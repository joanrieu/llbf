#include <iostream>
#include <fstream>
#include <stack>
#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/CommandLine.h>

class Compiler {

        public:

        Compiler();
        void Compile(std::istream& input);
        void Compile(char c);
        void TerminateAndOutput(std::ostream& output);

        private:

        llvm::Module module;
        llvm::IRBuilder<> builder;

        void DeclareStdio();
        llvm::Function* getchar;
        llvm::Function* putchar;

        void DefineCell();
        llvm::StructType* cellTy;
        llvm::Value* current;
        llvm::Value* currentCell;

        void DefineIO();
        llvm::Function* in;
        llvm::Function* out;

        void DefineAlloc();
        llvm::Function* malloc;
        llvm::Function* allocCell;

        void DefineMoves();
        void DefineMove(llvm::Function*& function, const llvm::Twine& name, bool forward);
        void Move(bool forward);
        llvm::Function* moveForward;
        llvm::Function* moveBackward;

        void LoopBegin();
        void LoopEnd();
        std::stack<llvm::PHINode*> loopPHIs;

        void PrepareMain();

};

Compiler::Compiler(): module("Brainfuck", llvm::getGlobalContext()), builder(module.getContext()) {

        DeclareStdio();
        DefineCell();
        DefineIO();
        DefineAlloc();
        DefineMoves();
        PrepareMain();

}

void Compiler::DeclareStdio() {

        // Three functions (getchar, putchar and malloc) from the C standard library are used.
        module.addLibrary("c");

        getchar = llvm::Function::Create(llvm::FunctionType::get(builder.getInt32Ty(), false), llvm::GlobalValue::ExternalLinkage, "getchar", &module);
        putchar = llvm::Function::Create(llvm::FunctionType::get(builder.getInt32Ty(), builder.getInt8Ty(), false), llvm::GlobalValue::ExternalLinkage, "putchar", &module);

}

void Compiler::DefineCell() {

        /* Cell:
         * - value
         * - previous
         * - next
         */

        cellTy = llvm::StructType::create(module.getContext(), "Cell");
        cellTy->setBody(builder.getInt8Ty(), cellTy->getPointerTo(), cellTy->getPointerTo(), 0);

}

void Compiler::DefineIO() {

        // Wrap getchar.
        in = llvm::Function::Create(llvm::FunctionType::get(builder.getInt8Ty(), false), llvm::GlobalValue::InternalLinkage, "in", &module);
        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "", in));
        builder.CreateRet(builder.CreateTruncOrBitCast(builder.CreateCall(getchar), builder.getInt8Ty()));

        // Wrap putchar.
        out = llvm::Function::Create(llvm::FunctionType::get(builder.getVoidTy(), builder.getInt8Ty(), false), llvm::GlobalValue::InternalLinkage, "out", &module);
        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "", out));
        builder.CreateCall(putchar, out->arg_begin());
        builder.CreateRetVoid();

}

void Compiler::DefineAlloc() {

        // Declare the C standard malloc function.
        // XXX It returns a Cell* because void* is not allowed anyway.
        // The LLVM doc says an i8* should be used, but cellTy* is easier here.

        llvm::Type* sizeTy = llvm::IntegerType::get(module.getContext(), sizeof(size_t) * 8);
        malloc = llvm::Function::Create(llvm::FunctionType::get(cellTy->getPointerTo(), sizeTy, false), llvm::GlobalValue::ExternalLinkage, "malloc", &module);

        // Define the cell allocation and creation function.

        llvm::Type* args[] = { cellTy->getPointerTo(), cellTy->getPointerTo() };
        allocCell = llvm::Function::Create(llvm::FunctionType::get(cellTy->getPointerTo(), args, false), llvm::GlobalValue::InternalLinkage, "allocCell", &module);

        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "", allocCell));
        llvm::Value* cellPtr = builder.CreateCall(malloc, builder.CreatePtrToInt(builder.CreateGEP(llvm::ConstantPointerNull::get(cellTy->getPointerTo()), builder.getInt32(1)), sizeTy));
        builder.CreateStore(builder.getInt8(0), builder.CreateStructGEP(cellPtr, 0));
        builder.CreateStore(allocCell->arg_begin(), builder.CreateStructGEP(cellPtr, 1));
        builder.CreateStore(++allocCell->arg_begin(), builder.CreateStructGEP(cellPtr, 2));
        builder.CreateRet(cellPtr);

}

void Compiler::DefineMoves() {

        DefineMove(moveForward, "moveForward", true);
        DefineMove(moveBackward, "moveBackward", false);

}

void Compiler::DefineMove(llvm::Function*& function, const llvm::Twine& name, bool forward) {

        // The function takes the current cell and returns the other one.
        function = llvm::Function::Create(llvm::FunctionType::get(cellTy->getPointerTo(), cellTy->getPointerTo(), false), llvm::GlobalValue::InternalLinkage, name, &module);
        llvm::Value* origin = function->arg_begin();

        // Retrieve the pointer to the other cell.
        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "", function));
        llvm::Value* oldPtrPtr = builder.CreateStructGEP(origin, forward ? 2 : 1);
        llvm::Value* oldPtr = builder.CreateLoad(oldPtrPtr);
        llvm::BranchInst* allocBr = builder.CreateCondBr(builder.CreateIsNotNull(oldPtr), llvm::BasicBlock::Create(module.getContext(), "existing", function), llvm::BasicBlock::Create(module.getContext(), "alloc", function));

        // Either the cell already exists.
        builder.SetInsertPoint(allocBr->getSuccessor(0));
        builder.CreateRet(oldPtr);
        
        // Or the cell needs to be allocated.
        builder.SetInsertPoint(allocBr->getSuccessor(1));
        llvm::Value* newPtr = builder.CreateCall2(allocCell, forward ? origin : llvm::ConstantPointerNull::get(cellTy->getPointerTo()), forward ? llvm::ConstantPointerNull::get(cellTy->getPointerTo()) : origin);
        builder.CreateStore(newPtr, oldPtrPtr);
        builder.CreateRet(newPtr);

}

void Compiler::PrepareMain() {

        // Declare the main function (which will be filled by the input).
        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "entry", llvm::Function::Create(llvm::FunctionType::get(builder.getInt32Ty(), false), llvm::GlobalValue::ExternalLinkage, "main", &module)));

        // Create the first cell.
        currentCell = builder.CreateCall2(allocCell, llvm::ConstantPointerNull::get(cellTy->getPointerTo()), llvm::ConstantPointerNull::get(cellTy->getPointerTo()), "currentCell");
        current = builder.getInt8(0);

}

void Compiler::Compile(std::istream& input) {

        char c;
        while (input.get(c))
                Compile(c);

}

void Compiler::Compile(char c) {

        switch (c) {

                case '+':
                current = builder.CreateAdd(current, builder.getInt8(1), "current");
                break;

                case '-':
                current = builder.CreateSub(current, builder.getInt8(1), "current");
                break;

                case ',':
                current = builder.CreateCall(in, "current");
                break;

                case '.':
                builder.CreateCall(out, current);
                break;

                case '>':
                Move(true);
                break;

                case '<':
                Move(false);
                break;

                case '[':
                LoopBegin();
                break;

                case ']':
                LoopEnd();
                break;

                default:
                break;

        }

}

void Compiler::Move(bool forward) {

        // Store the old value, change the cell and load the new value.
        builder.CreateStore(current, builder.CreateStructGEP(currentCell, 0, "oldCellValuePtr"));
        currentCell = builder.CreateCall(forward ? moveForward : moveBackward, currentCell, "currentCell");
        current = builder.CreateLoad(builder.CreateStructGEP(currentCell, 0, "currentCellValuePtr"), "current");

}

void Compiler::LoopBegin() {

        /* Loop structure:
         * - caller: the part before the loop;
         * - head: jumps either to the body or to the end;
         * - body: part inside the loop;
         * - end: part after the loop.
         */

        /* Method:
         * Terminate the caller by a jump to the head.
         * In the head, get the state either from caller or body.
         * The former is the current one, the latter will be added in LoopEnd.
         * (That's why the PHI nodes are put on a stack.)
         * Terminate the head by a jump either to the body or the end.
         */

        llvm::BasicBlock* loopCaller = builder.GetInsertBlock();
        llvm::BasicBlock* loopHead = llvm::BasicBlock::Create(module.getContext(), "loop.head", loopCaller->getParent());
        llvm::BasicBlock* loopBody = llvm::BasicBlock::Create(module.getContext(), "loop.body", loopCaller->getParent());
        llvm::BasicBlock* loopEnd = llvm::BasicBlock::Create(module.getContext(), "loop.end", loopCaller->getParent());

        builder.CreateBr(loopHead);

        builder.SetInsertPoint(loopHead);

        loopPHIs.push(builder.CreatePHI(cellTy->getPointerTo(), 2, "currentCell"));
        loopPHIs.top()->addIncoming(currentCell, loopCaller);
        currentCell = loopPHIs.top();

        loopPHIs.push(builder.CreatePHI(builder.getInt8Ty(), 2, "current"));
        loopPHIs.top()->addIncoming(current, loopCaller);
        current = loopPHIs.top();

        builder.CreateCondBr(builder.CreateICmpNE(current, builder.getInt8(0)), loopBody, loopEnd);

        builder.SetInsertPoint(loopBody);

}

void Compiler::LoopEnd() {

        /* Method:
         * Terminate the body by jumping to the head.
         * Give the current state to the head.
         * (Fill the PHI nodes and pop them from the stack.)
         */

        llvm::BasicBlock* loopHead = loopPHIs.top()->getParent();
        llvm::BasicBlock* loopBody = builder.GetInsertBlock();
        llvm::BasicBlock* loopEnd = loopHead->getTerminator()->getSuccessor(1);

        builder.CreateBr(loopHead);

        loopPHIs.top()->addIncoming(current, loopBody);
        current = loopPHIs.top();
        loopPHIs.pop();

        loopPHIs.top()->addIncoming(currentCell, loopBody);
        currentCell = loopPHIs.top();
        loopPHIs.pop();

        builder.SetInsertPoint(loopEnd);

}

void Compiler::TerminateAndOutput(std::ostream& output) {

        // Terminate the function and output the module.

        builder.CreateRet(builder.getInt32(0));

        llvm::raw_os_ostream out(output);
        module.print(out, 0);

}

llvm::cl::opt<std::string> InputFilename(llvm::cl::Positional, llvm::cl::desc("<input file>"), llvm::cl::init("-"));
llvm::cl::opt<std::string> OutputFilename(llvm::cl::Prefix, "o", llvm::cl::desc("Specify output filename"), llvm::cl::value_desc("filename"), llvm::cl::init("-"));

int main(int argc, char** argv) {

        llvm::cl::ParseCommandLineOptions(argc, argv);

        Compiler cmp;

        if (InputFilename != "-") {
                std::ifstream file(InputFilename.c_str());
                cmp.Compile(file);
        } else cmp.Compile(std::cin);

        if (OutputFilename != "-") {
                std::ofstream file(OutputFilename.c_str());
                cmp.TerminateAndOutput(file);
        } else cmp.TerminateAndOutput(std::cout);

        return 0;

}
