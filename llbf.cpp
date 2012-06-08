#include <fstream>
#include <iostream>
#include <stack>

#include <llvm/LLVMContext.h>
#include <llvm/Module.h>
#include <llvm/PassManager.h>
#include <llvm/Bitcode/ReaderWriter.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/IRBuilder.h>
#include <llvm/Support/SystemUtils.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Transforms/IPO.h>

#ifdef LLBF_JIT

#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Support/TargetSelect.h>

#endif

class Compiler {

        public:

        Compiler();
        void Compile(char c, std::string& error);
        void Terminate(std::string& error);
        void Output(llvm::raw_ostream& stream, bool humanReadable) const;
        void Run(std::string& error);

        private:

        llvm::Module module;
        llvm::IRBuilder<> builder;

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
        void LoopEnd(std::string& error);
        std::stack<llvm::PHINode*> loopPHIs;

        void PrepareMain();

};

Compiler::Compiler(): module("Brainfuck", llvm::getGlobalContext()), builder(module.getContext()) {

        DefineCell();
        DefineIO();
        DefineAlloc();
        DefineMoves();
        PrepareMain();

}

void Compiler::DefineCell() {

        /* Cell:
         * - value
         * - previous
         * - next
         */

        cellTy = llvm::StructType::create(module.getContext(), "Cell");
        cellTy->setBody(builder.getInt8Ty(), cellTy->getPointerTo(), cellTy->getPointerTo(), NULL);

}

void Compiler::DefineIO() {

        // Functions from the C standard library are used.
        module.addLibrary("c");

        // Wrap getchar.

        llvm::Function* getchar = llvm::Function::Create(llvm::FunctionType::get(builder.getInt32Ty(), false), llvm::GlobalValue::ExternalLinkage, "getchar", &module);

        in = llvm::Function::Create(llvm::FunctionType::get(builder.getInt8Ty(), false), llvm::GlobalValue::InternalLinkage, "in", &module);
        in->addFnAttr(llvm::Attribute::AlwaysInline);

        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "", in));
        builder.CreateRet(builder.CreateTruncOrBitCast(builder.CreateCall(getchar), builder.getInt8Ty()));

        // Wrap putchar.

        llvm::Function* putchar = llvm::Function::Create(llvm::FunctionType::get(builder.getInt32Ty(), builder.getInt8Ty(), false), llvm::GlobalValue::ExternalLinkage, "putchar", &module);

        out = llvm::Function::Create(llvm::FunctionType::get(builder.getVoidTy(), builder.getInt8Ty(), false), llvm::GlobalValue::InternalLinkage, "out", &module);
        out->addFnAttr(llvm::Attribute::AlwaysInline);

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
        allocCell->addFnAttr(llvm::Attribute::AlwaysInline);

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
        function->setCallingConv(llvm::CallingConv::Fast);

        // Retrieve the pointer to the other cell.
        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "", function));
        llvm::Value* origin = function->arg_begin();
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
        builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "entry", llvm::Function::Create(llvm::FunctionType::get(builder.getVoidTy(), false), llvm::GlobalValue::ExternalLinkage, "main", &module)));

        // Create the first cell.
        currentCell = builder.CreateCall2(allocCell, llvm::ConstantPointerNull::get(cellTy->getPointerTo()), llvm::ConstantPointerNull::get(cellTy->getPointerTo()), "currentCell");
        current = builder.getInt8(0);

}

void Compiler::Compile(char c, std::string& error) {

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
                LoopEnd(error);
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

void Compiler::LoopEnd(std::string& error) {

        if (loopPHIs.empty()) {
                error = "unexpected ']'";
                return;
        }

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

void Compiler::Terminate(std::string& error) {

        if (not loopPHIs.empty()) {
                error = "expected ']' before EOF";
                return;
        }

        builder.CreateRetVoid();

        llvm::PassManager pm;
        pm.add(llvm::createAlwaysInlinerPass());
        pm.run(module);

}

void Compiler::Output(llvm::raw_ostream& stream, bool humanReadable) const {

        if (humanReadable)
                module.print(stream, 0);
        else
                llvm::WriteBitcodeToFile(&module, stream);

}

#ifdef LLBF_JIT

void Compiler::Run(std::string& error) {

        llvm::InitializeNativeTarget();
        llvm::ExecutionEngine* engine = llvm::EngineBuilder(&module).create();

        if (not engine) {
                error = "Error creating execution engine!";
                return;
        }

        void* main = engine->getPointerToFunction(builder.GetInsertBlock()->getParent());

        if (not main) {
                error = "Error compiling to machine code!";
                return;
        }

        reinterpret_cast<void(*)()>(main)();

}

#endif

llvm::cl::opt<std::string> InputFilename(llvm::cl::Positional, llvm::cl::desc("<input file>"), llvm::cl::init("-"));
llvm::cl::opt<std::string> OutputFilename(llvm::cl::Prefix, "o", llvm::cl::desc("Specify output filename"), llvm::cl::value_desc("filename"), llvm::cl::init("-"));
llvm::cl::opt<bool> HumanReadable(llvm::cl::Optional, "S", llvm::cl::desc("Write output in LLVM intermediate language (instead of bitcode)"));
llvm::cl::opt<bool> ForceOutput(llvm::cl::Optional, "f", llvm::cl::desc("Enable binary output on terminals"));

#ifdef LLBF_JIT
llvm::cl::opt<bool> RunProgram(llvm::cl::Optional, "run", llvm::cl::desc("Run the program"));
#endif

int main(int argc, char** argv) {

        enum Errors {
                LLBF_ERROR_NONE = EXIT_SUCCESS,
                LLBF_ERROR_IO,
                LLBF_ERROR_SYNTAX,
#ifdef LLBF_JIT
                LLBF_ERROR_JIT
#endif
        };

        std::string error;

        llvm::cl::ParseCommandLineOptions(argc, argv,
#ifdef LLBF_JIT
                "Brainfuck compiler with JIT support based on LLVM\n"
#else
                "Brainfuck compiler based on LLVM\n"
#endif
        );

        std::ifstream file;
        const bool ReadStdin = InputFilename == "-";
        const char* InputName = ReadStdin ? "<stdin>" : InputFilename.c_str();

        if (not ReadStdin) {
                file.open(InputFilename.c_str());
                if (not file.is_open()) {
                        std::cerr << "Error opening input file: " << InputFilename << std::endl;
                        return LLBF_ERROR_IO;
                }
        };

        Compiler cmp;
        std::istream& input = ReadStdin ? std::cin : file;

        unsigned ln = 1, cn = 0;
        char c;
        while (input.get(c)) {

                if (c == '\n') {
                        ++ln, cn = 0;
                        continue;
                }

                ++cn;

                cmp.Compile(c, error);

                if (not error.empty()) {
                        std::cerr << InputName << ':' << ln << ':' << cn << ": error: " << error << std::endl;
                        return LLBF_ERROR_SYNTAX;
                }

        }

        if (not input.eof()) {
                std::cerr << "Error reading input file: " << InputName << std::endl;
                return LLBF_ERROR_IO;
        }

        cmp.Terminate(error);

        if (not error.empty()) {
                std::cerr << InputName << ':' << ln << ':' << cn << ": error: " << error << std::endl;
                return LLBF_ERROR_SYNTAX;
        }

#ifdef LLBF_JIT

        if (RunProgram) {

                cmp.Run(error);

                if (error.empty())
                        return LLBF_ERROR_NONE;

                std::cerr << error << std::endl;
                return LLBF_ERROR_JIT;

        }

#endif

        llvm::tool_output_file output(OutputFilename.c_str(), error, HumanReadable ? 0 : llvm::raw_fd_ostream::F_Binary);

        if (not error.empty()) {
                std::cerr << error << std::endl;
                return LLBF_ERROR_IO;
        }

        if (not HumanReadable and not ForceOutput and llvm::CheckBitcodeOutputToConsole(output.os()))
                return LLBF_ERROR_IO;

        cmp.Output(output.os(), HumanReadable);
        output.keep();

        return LLBF_ERROR_NONE;

}
