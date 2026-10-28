"""KairoTowns's local changes to the Tsugaru snapshot in third_party/tsugaru, each marked KairoTowns: in the code, which shared/tools/local_patches.py applies and checks. Recorded from the snapshot as committed; docs/source-import.md describes each change."""

# One group per changed file: (path under third_party/, old text, new text) in order.
PATCHES = {
    'tsugaru/src/cpu/i486.h': [
        ('tsugaru/src/cpu/i486.h',
         '''	};
	INT21HInterceptor *int21HInterceptorPtr=nullptr;


	enum
	{
''',
         '''	};
	INT21HInterceptor *int21HInterceptorPtr=nullptr;

	/*! KairoTowns: counts INT 21H calls with AH=0AH (buffered line input), as the guest waiting
	    for a typed line; startup commands are typed then (core/towns_typing.h).
	*/
	uint64_t kairoLineInputs=0;


	enum
	{
'''),
        ('tsugaru/src/cpu/i486.h',
         '''		Operand op2;
	};

	/*! OperandValue class is an evaluated operand value, or a value to be stored to
	    the destination described by the operand.
	    In 80486, operand itself may not know its size if it is an address operand.
''',
         '''		Operand op2;
	};

	/*! KairoTowns: instructions decoded from code read directly through a memory window, by the
	    host address of their first byte. Decoding such code is a function of its bytes and the
	    default operand and address sizes alone, so an entry whose bytes still match the code is
	    the decode of the code (FetchInstructionClass::FetchInstruction). Not saved in states.
	*/
	class DecodedInstruction
	{
	public:
		const unsigned char *code=nullptr;
		unsigned int defOperSize=0,defAddrSize=0;
		// The instruction's bytes as bytes 0-7 and 7-14 of the code, each masked to the bytes the
		// instruction has; MatchesCode compares them with two loads.
		uint64_t low=0,high=0,lowMask=0,highMask=0;
		InstructionAndOperand instOp;

		static inline uint64_t Load(const unsigned char *p)
		{
			uint64_t v;
			memcpy(&v,p,8);
			return v;
		}
		// Masks of the first n bytes of bytes 0-7 and of bytes 7-14, as loaded little-endian.
		static inline uint64_t LowMask(unsigned int n)
		{
			return (8<=n ? ~(uint64_t)0 : (((uint64_t)1<<(8*n))-1));
		}
		static inline uint64_t HighMask(unsigned int n)
		{
			return (n<=8 ? 0 : (((n-7)>=8 ? ~(uint64_t)0 : (((uint64_t)1<<(8*(n-7)))-1))));
		}
		// code must have at least MAX_INSTRUCTION_LENGTH readable bytes.
		inline bool MatchesCode(const unsigned char *p) const
		{
			return ((Load(p)&lowMask)==low && (Load(p+7)&highMask)==high);
		}
		inline void Store(const unsigned char *p,unsigned int numBytes)
		{
			lowMask=LowMask(numBytes);
			highMask=HighMask(numBytes);
			low=(Load(p)&lowMask);
			high=(Load(p+7)&highMask);
		}
	};
	enum
	{
		DECODE_CACHE_SIZE=16384,
	};
	mutable std::vector <DecodedInstruction> decodeCache;

	/*! OperandValue class is an evaluated operand value, or a value to be stored to
	    the destination described by the operand.
	    In 80486, operand itself may not know its size if it is an address operand.
'''),
        ('tsugaru/src/cpu/i486.h',
         '''
	/*! Returns the addressing size (16 or 32) of the stack segment.
	*/
	inline unsigned int GetStackAddressingSize(void) const
	{
		return *stackAddressSizePointer[Return0InRealMode1InProtectedMode()];
''',
         '''
	/*! Returns the addressing size (16 or 32) of the stack segment.
	*/
	/*! KairoTowns: the default operand and address sizes of the code segment, as instructions
	    are decoded with them, for the JIT (core/jit).
	*/
	inline unsigned int KairoCodeOperandSize(void) const
	{
		return *CSOperandSizePointer[Return0InRealMode1InProtectedMode()];
	}
	inline unsigned int KairoCodeAddressSize(void) const
	{
		return *CSAddressSizePointer[Return0InRealMode1InProtectedMode()];
	}

	inline unsigned int GetStackAddressingSize(void) const
	{
		return *stackAddressSizePointer[Return0InRealMode1InProtectedMode()];
'''),
        ('tsugaru/src/cpu/i486.h',
         '''	static void FetchOperandLoop(CPUCLASS &cpu,InstructionAndOperand &instOp,MemoryAccess::ConstPointer &ptr,const SegmentRegister &seg,int offset,MEMCLASS &mem,unsigned int defOperSize,unsigned int defAddrSize);

public:
	/*! Fetch an instruction from specific segment and offset.
	    It will not affect the CPU state including exceptions.
	*/
''',
         '''	static void FetchOperandLoop(CPUCLASS &cpu,InstructionAndOperand &instOp,MemoryAccess::ConstPointer &ptr,const SegmentRegister &seg,int offset,MEMCLASS &mem,unsigned int defOperSize,unsigned int defAddrSize);

public:
	/*! KairoTowns: FetchOperandLoop, for the JIT (core/jit), which decodes code exactly as the
	    interpreter does before it translates it.
	*/
	template <class CPUCLASS,class MEMCLASS,class FUNCCLASS>
	static void KairoFetchOperandLoop(CPUCLASS &cpu,InstructionAndOperand &instOp,MemoryAccess::ConstPointer &ptr,const SegmentRegister &seg,int offset,MEMCLASS &mem,unsigned int defOperSize,unsigned int defAddrSize)
	{
		FetchOperandLoop<CPUCLASS,MEMCLASS,FUNCCLASS>(cpu,instOp,ptr,seg,offset,mem,defOperSize,defAddrSize);
	}

	/*! Fetch an instruction from specific segment and offset.
	    It will not affect the CPU state including exceptions.
	*/
'''),
        ('tsugaru/src/cpu/i486.h',
         '''template <class FIDELITY>
inline void i486DXFidelityLayer<FIDELITY>::StoreByte(Memory &mem,int addressSize,const SegmentRegister &seg,unsigned int offset,unsigned char byteData)
{
	AddressMask(offset,(unsigned char)addressSize);
	auto linearAddr=seg.baseLinearAddr+offset;

''',
         '''template <class FIDELITY>
inline void i486DXFidelityLayer<FIDELITY>::StoreByte(Memory &mem,int addressSize,const SegmentRegister &seg,unsigned int offset,unsigned char byteData)
{
	++mem.sideEffects; // KairoTowns: stores through SSESPWindow bypass Memory.
	AddressMask(offset,(unsigned char)addressSize);
	auto linearAddr=seg.baseLinearAddr+offset;

'''),
        ('tsugaru/src/cpu/i486.h',
         '''	   // nullptr!=state.SSESPWindow.ptr &&
	   true==state.SSESPWindow.IsLinearAddressInRange(linearAddr))
	{
		state.SSESPWindow.ptr[linearAddr&(MemoryAccess::MEMORY_WINDOW_SIZE-1)]=byteData;
		return;
	}
''',
         '''	   // nullptr!=state.SSESPWindow.ptr &&
	   true==state.SSESPWindow.IsLinearAddressInRange(linearAddr))
	{
		mem.KairoWritten(state.SSESPWindow.ptr+(linearAddr&(MemoryAccess::MEMORY_WINDOW_SIZE-1)),1); // KairoTowns: Memory counts its own.
		state.SSESPWindow.ptr[linearAddr&(MemoryAccess::MEMORY_WINDOW_SIZE-1)]=byteData;
		return;
	}
'''),
        ('tsugaru/src/cpu/i486.h',
         '''template <class FIDELITY>
inline void i486DXFidelityLayer<FIDELITY>::StoreWord(Memory &mem,int addressSize,const SegmentRegister &seg,unsigned int offset,unsigned int data)
{
	AddressMask(offset,(unsigned char)addressSize);
	auto linearAddr=seg.baseLinearAddr+offset;

''',
         '''template <class FIDELITY>
inline void i486DXFidelityLayer<FIDELITY>::StoreWord(Memory &mem,int addressSize,const SegmentRegister &seg,unsigned int offset,unsigned int data)
{
	++mem.sideEffects; // KairoTowns: stores through SSESPWindow bypass Memory.
	AddressMask(offset,(unsigned char)addressSize);
	auto linearAddr=seg.baseLinearAddr+offset;

'''),
        ('tsugaru/src/cpu/i486.h',
         '''		unsigned int low12bits=(linearAddr&(MemoryAccess::MEMORY_WINDOW_SIZE-1));
		if(low12bits<=MemoryAccess::MEMORY_WINDOW_SIZE-2)
		{
			cpputil::PutWord(state.SSESPWindow.ptr+low12bits,data);
			return;
		}
''',
         '''		unsigned int low12bits=(linearAddr&(MemoryAccess::MEMORY_WINDOW_SIZE-1));
		if(low12bits<=MemoryAccess::MEMORY_WINDOW_SIZE-2)
		{
			mem.KairoWritten(state.SSESPWindow.ptr+low12bits,2); // KairoTowns: Memory counts its own.
			cpputil::PutWord(state.SSESPWindow.ptr+low12bits,data);
			return;
		}
'''),
        ('tsugaru/src/cpu/i486.h',
         '''template <class FIDELITY>
inline void i486DXFidelityLayer<FIDELITY>::StoreDword(Memory &mem,int addressSize,const SegmentRegister &seg,unsigned int offset,unsigned int data)
{
	AddressMask(offset,(unsigned char)addressSize);
	auto linearAddr=seg.baseLinearAddr+offset;

''',
         '''template <class FIDELITY>
inline void i486DXFidelityLayer<FIDELITY>::StoreDword(Memory &mem,int addressSize,const SegmentRegister &seg,unsigned int offset,unsigned int data)
{
	++mem.sideEffects; // KairoTowns: stores through SSESPWindow bypass Memory.
	AddressMask(offset,(unsigned char)addressSize);
	auto linearAddr=seg.baseLinearAddr+offset;

'''),
        ('tsugaru/src/cpu/i486.h',
         '''		unsigned int low12bits=(linearAddr&(MemoryAccess::MEMORY_WINDOW_SIZE-1));
		if(low12bits<=MemoryAccess::MEMORY_WINDOW_SIZE-4)
		{
			cpputil::PutDword(state.SSESPWindow.ptr+low12bits,data);
			return;
		}
''',
         '''		unsigned int low12bits=(linearAddr&(MemoryAccess::MEMORY_WINDOW_SIZE-1));
		if(low12bits<=MemoryAccess::MEMORY_WINDOW_SIZE-4)
		{
			mem.KairoWritten(state.SSESPWindow.ptr+low12bits,4); // KairoTowns: Memory counts its own.
			cpputil::PutDword(state.SSESPWindow.ptr+low12bits,data);
			return;
		}
'''),
    ],
    'tsugaru/src/cpu/i486instfetch.h': [
        ('tsugaru/src/cpu/i486instfetch.h',
         '''class i486DXCommon::FetchInstructionClass
{
public:
	static inline void FetchInstruction(
	   CPUCLASS &cpu,
	   MemoryAccess::ConstMemoryWindow &memWin,
	   InstructionAndOperand &instOp,
''',
         '''class i486DXCommon::FetchInstructionClass
{
public:
	/*! KairoTowns: the instruction at CS:offset, decoded: a decode the cache holds, used in place,
	    or instOp filled with a fresh one. A cached decode stays as it is until the next fetch, so the
	    caller reads it before fetching again.
	*/
	static inline const InstructionAndOperand &FetchInstructionRef(
	   CPUCLASS &cpu,
	   MemoryAccess::ConstMemoryWindow &memWin,
	   InstructionAndOperand &instOp,
'''),
        ('tsugaru/src/cpu/i486instfetch.h',
         '''	{
		auto &inst=instOp.inst;

		inst.Clear();
		inst.operandSize=defOperSize;
		inst.addressSize=defAddrSize;
		inst.codeAddressSize=defAddrSize;

		// Question: Do prefixes need to be in the specific order INST_PREFIX->ADDRSIZE_OVERRIDE->OPSIZE_OVERRIDE->SEG_OVERRIDE?

		auto CSEIPLinear=CS.baseLinearAddr+offset;
		if(true!=memWin.IsLinearAddressInRange(CSEIPLinear))
		{
			FUNCCLASS::GetConstMemoryWindow(cpu,memWin,inst.codeAddressSize,CS,offset,mem);
			// If Page Fault, do not bother fetch instruction.
			// By fetching, Page Fault may turn into Segmentation Violation.
			// Page Fault must be prioritize.
			if(true==FUNCCLASS::CheckExceptionInHighFidelityMode(cpu))
			{
				return; // Page Fault -> Don't look at memory at all.
			}
		}
		auto ptr=memWin.GetReadAccessPointer(CSEIPLinear);

	#ifdef VERIFY_MEMORY_WINDOW
		MemoryAccess::ConstMemoryWindow testMemWin;
		FUNCCLASS::GetConstMemoryWindow(cpu,testMemWin,inst.codeAddressSize,CS,offset,mem);
		if(testMemWin.linearBaseAddr!=memWin.linearBaseAddr || testMemWin.ptr!=memWin.ptr)
		{
			std::cout << "Memory Window Test failed!" << std::endl;
''',
         '''	{
		auto &inst=instOp.inst;

		// KairoTowns: the whole instruction and both operands start at zero. Clear() set only some
		// fields, so fields an instruction's decode does not set (an operand it does not have) kept
		// whatever the caller's stack held, and executing code that reads them made runs differ
		// from launch to launch (KairoTowns#32); a cached decode would also have kept them. A cached
		// decode replaces the whole of instOp, so an instruction found in the cache skips this.
		bool started=false;
		auto start=[&]()
		{
			memset(&instOp,0,sizeof(instOp));
			inst.Clear();
			inst.operandSize=defOperSize;
			inst.addressSize=defAddrSize;
			inst.codeAddressSize=defAddrSize;
			started=true;
		};

		// Question: Do prefixes need to be in the specific order INST_PREFIX->ADDRSIZE_OVERRIDE->OPSIZE_OVERRIDE->SEG_OVERRIDE?

		auto CSEIPLinear=CS.baseLinearAddr+offset;
		if(true!=memWin.IsLinearAddressInRange(CSEIPLinear))
		{
			start();
			FUNCCLASS::GetConstMemoryWindow(cpu,memWin,inst.codeAddressSize,CS,offset,mem);
			// If Page Fault, do not bother fetch instruction.
			// By fetching, Page Fault may turn into Segmentation Violation.
			// Page Fault must be prioritize.
			if(true==FUNCCLASS::CheckExceptionInHighFidelityMode(cpu))
			{
				return instOp; // Page Fault -> Don't look at memory at all.
			}
		}
		auto ptr=memWin.GetReadAccessPointer(CSEIPLinear);

	#ifdef VERIFY_MEMORY_WINDOW
		MemoryAccess::ConstMemoryWindow testMemWin;
		FUNCCLASS::GetConstMemoryWindow(cpu,testMemWin,defAddrSize,CS,offset,mem);
		if(testMemWin.linearBaseAddr!=memWin.linearBaseAddr || testMemWin.ptr!=memWin.ptr)
		{
			std::cout << "Memory Window Test failed!" << std::endl;
'''),
        ('tsugaru/src/cpu/i486instfetch.h',
         '''			std::cout << "CS:EIP Memory Window Linear Addr:   " << cpputil::Uitox(testMemWin.linearBaseAddr) << std::endl;
			std::cout << "Pointer Diff " << cpputil::Uitox(memWin.ptr-testMemWin.ptr) << std::endl;
			Abort("Memory Cache Failed.");
			return;
		}
	#endif

''',
         '''			std::cout << "CS:EIP Memory Window Linear Addr:   " << cpputil::Uitox(testMemWin.linearBaseAddr) << std::endl;
			std::cout << "Pointer Diff " << cpputil::Uitox(memWin.ptr-testMemWin.ptr) << std::endl;
			Abort("Memory Cache Failed.");
			return instOp;
		}
	#endif

'''),
        ('tsugaru/src/cpu/i486instfetch.h',
         '''		// According to the sample taken 2024/06/07, burst-mode is used for roughly 99.96% of the instructions.
		if(MAX_INSTRUCTION_LENGTH<=ptr.length)
		{
			inst.opCode=BURSTMODEFUNCCLASS::FetchInstructionByte(cpu,ptr,inst.codeAddressSize,CS,offset,mem);
			CPUCLASS::template FetchOperandLoop<CPUCLASS,MEMCLASS,BURSTMODEFUNCCLASS>(cpu,instOp,ptr,CS,offset+1,mem,defOperSize,defAddrSize);
			// BurstModeFetchInstructionFunctions does not update ptr.length.
		}
		else
		{
			inst.opCode=FUNCCLASS::FetchInstructionByte(cpu,ptr,inst.codeAddressSize,CS,offset,mem);
			CPUCLASS::template FetchOperandLoop<CPUCLASS,MEMCLASS,FUNCCLASS>(cpu,instOp,ptr,CS,offset+1,mem,defOperSize,defAddrSize);
		}
	}
};

''',
         '\t\t// According to the sample taken 2024/06/07, burst-mode is used for roughly 99.96% of the instructions.\n\t\tif(MAX_INSTRUCTION_LENGTH<=ptr.length)\n\t\t{\n\t\t\t// KairoTowns: the decode of code that matches a cached decode is that decode\n\t\t\t// (i486DXCommon::DecodedInstruction). KAIROTOWNS_NO_DECODE_CACHE builds without it, to\n\t\t\t// compare.\n\t\t#ifndef KAIROTOWNS_NO_DECODE_CACHE\n\t\t\tif(true==cpu.decodeCache.empty())\n\t\t\t{\n\t\t\t\tcpu.decodeCache.resize(DECODE_CACHE_SIZE);\n\t\t\t}\n\t\t\tconst unsigned char *code=ptr.ptr;\n\t\t\tauto &cached=cpu.decodeCache[(reinterpret_cast<uintptr_t>(code))&(DECODE_CACHE_SIZE-1)];\n\t\t\tif(cached.code==code && cached.defOperSize==defOperSize && cached.defAddrSize==defAddrSize &&\n\t\t\t   true==cached.MatchesCode(code))\n\t\t\t{\n\t\t\t#ifdef KAIROTOWNS_VERIFY_DECODE_CACHE\n\t\t\t\t// KairoTowns: decodes the code again and stops when the cached decode differs.\n\t\t\t\tif(true!=started)\n\t\t\t\t{\n\t\t\t\t\tstart();\n\t\t\t\t}\n\t\t\t\tinst.opCode=BURSTMODEFUNCCLASS::FetchInstructionByte(cpu,ptr,inst.codeAddressSize,CS,offset,mem);\n\t\t\t\tCPUCLASS::template FetchOperandLoop<CPUCLASS,MEMCLASS,BURSTMODEFUNCCLASS>(cpu,instOp,ptr,CS,offset+1,mem,defOperSize,defAddrSize);\n\t\t\t\tif(0!=memcmp(&instOp,&cached.instOp,sizeof(instOp)))\n\t\t\t\t{\n\t\t\t\t\tfprintf(stderr,"Decode cache differs at %p opcode %x\\n",(const void *)code,inst.opCode);\n\t\t\t\t\tabort();\n\t\t\t\t}\n\t\t\t#endif\n\t\t\t\treturn cached.instOp;\n\t\t\t}\n\t\t\tif(true!=started)\n\t\t\t{\n\t\t\t\tstart();\n\t\t\t}\n\t\t\tinst.opCode=BURSTMODEFUNCCLASS::FetchInstructionByte(cpu,ptr,inst.codeAddressSize,CS,offset,mem);\n\t\t\tCPUCLASS::template FetchOperandLoop<CPUCLASS,MEMCLASS,BURSTMODEFUNCCLASS>(cpu,instOp,ptr,CS,offset+1,mem,defOperSize,defAddrSize);\n\t\t\t// BurstModeFetchInstructionFunctions does not update ptr.length.\n\t\t\tif(inst.numBytes<=MAX_INSTRUCTION_LENGTH)\n\t\t\t{\n\t\t\t\tcached.code=code;\n\t\t\t\tcached.defOperSize=defOperSize;\n\t\t\t\tcached.defAddrSize=defAddrSize;\n\t\t\t\tcached.Store(code,inst.numBytes);\n\t\t\t\tcached.instOp=instOp;\n\t\t\t}\n\t\t#else\n\t\t\tif(true!=started)\n\t\t\t{\n\t\t\t\tstart();\n\t\t\t}\n\t\t\tinst.opCode=BURSTMODEFUNCCLASS::FetchInstructionByte(cpu,ptr,inst.codeAddressSize,CS,offset,mem);\n\t\t\tCPUCLASS::template FetchOperandLoop<CPUCLASS,MEMCLASS,BURSTMODEFUNCCLASS>(cpu,instOp,ptr,CS,offset+1,mem,defOperSize,defAddrSize);\n\t\t#endif\n\t\t}\n\t\telse\n\t\t{\n\t\t\tif(true!=started)\n\t\t\t{\n\t\t\t\tstart();\n\t\t\t}\n\t\t\tinst.opCode=FUNCCLASS::FetchInstructionByte(cpu,ptr,inst.codeAddressSize,CS,offset,mem);\n\t\t\tCPUCLASS::template FetchOperandLoop<CPUCLASS,MEMCLASS,FUNCCLASS>(cpu,instOp,ptr,CS,offset+1,mem,defOperSize,defAddrSize);\n\t\t}\n\t\treturn instOp;\n\t}\n\n\tstatic inline void FetchInstruction(\n\t   CPUCLASS &cpu,\n\t   MemoryAccess::ConstMemoryWindow &memWin,\n\t   InstructionAndOperand &instOp,\n\t   const SegmentRegister &CS,unsigned int offset,MEMCLASS &mem,unsigned int defOperSize,unsigned int defAddrSize)\n\t{\n\t\tconst InstructionAndOperand &decoded=FetchInstructionRef(cpu,memWin,instOp,CS,offset,mem,defOperSize,defAddrSize);\n\t\tif(&decoded!=&instOp)\n\t\t{\n\t\t\tinstOp=decoded;\n\t\t}\n\t}\n};\n\n'),
    ],
    'tsugaru/src/cpu/i486runinstruction.h': [
        ('tsugaru/src/cpu/i486runinstruction.h',
         '}\n\n#define i486DXFidelityLayer__GetOperandPointerAddr(addrCond) \\\n\t\tunsigned int offset; \\\n\t\tconst SegmentRegister &seg=*ExtractSegmentAndOffset(offset,op,segmentOverride); \\\n\t\tif(16==addressSize) \\\n',
         '}\n\n#define i486DXFidelityLayer__GetOperandPointerAddr(addrCond) \\\n\t\tif(true==forWrite) /* KairoTowns: a store through the pointer bypasses Memory. */ \\\n\t\t{ \\\n\t\t\t++mem.sideEffects; \\\n\t\t} \\\n\t\tunsigned int offset; \\\n\t\tconst SegmentRegister &seg=*ExtractSegmentAndOffset(offset,op,segmentOverride); \\\n\t\tif(16==addressSize) \\\n'),
        ('tsugaru/src/cpu/i486runinstruction.h',
         '\t\t\tauto memWin=mem.GetMemoryWindow(physAddr); \\\n\t\t\tif(nullptr!=memWin.ptr) \\\n\t\t\t{ \\\n\t\t\t\treturn memWin.ptr+low12bit; \\\n\t\t\t} \\\n\t\t}\n',
         '\t\t\tauto memWin=mem.GetMemoryWindow(physAddr); \\\n\t\t\tif(nullptr!=memWin.ptr) \\\n\t\t\t{ \\\n\t\t\t\tif(true==forWrite) /* KairoTowns: at most 4 bytes, from here. */ \\\n\t\t\t\t{ \\\n\t\t\t\t\tmem.KairoWritten(memWin.ptr+low12bit,1); \\\n\t\t\t\t} \\\n\t\t\t\treturn memWin.ptr+low12bit; \\\n\t\t\t} \\\n\t\t}\n'),
        ('tsugaru/src/cpu/i486runinstruction.h',
         '''
	state.holdIRQ=false;

	InstructionAndOperand instOp;
	const auto &inst=instOp.inst;
	const auto &op1=instOp.op1;
	const auto &op2=instOp.op2;
	FetchInstruction(state.CSEIPWindow,instOp,state.CS(),state.EIP,mem);

	if(true==fidelity.HandleExceptionAfterFetchInstruction(*this,mem,inst,op1))
	{
''',
         '''
	state.holdIRQ=false;

	// KairoTowns: the decode is read in place from the decode cache when it is there, instead of
	// copied out of it; nothing fetches again before this instruction has run.
	InstructionAndOperand fetched;
	const InstructionAndOperand &instOp=FetchInstructionClass<i486DXFidelityLayer<FIDELITY>,Memory,RealFetchInstructionFunctions,BurstModeFetchInstructionFunctions>::FetchInstructionRef(
	    *this,state.CSEIPWindow,fetched,state.CS(),state.EIP,mem,
	    *CSOperandSizePointer[Return0InRealMode1InProtectedMode()],*CSAddressSizePointer[Return0InRealMode1InProtectedMode()]);
	const auto &inst=instOp.inst;
	const auto &op1=instOp.op1;
	const auto &op2=instOp.op2;

	if(true==fidelity.HandleExceptionAfterFetchInstruction(*this,mem,inst,op1))
	{
'''),
        ('tsugaru/src/cpu/i486runinstruction.h',
         '''			EIPIncrement=0;
			break;
		}
		Interrupt(inst.EvalUimm8(),mem,2,2,true);
		EIPIncrement=0;
		break;
''',
         '''			EIPIncrement=0;
			break;
		}
		// KairoTowns: DOS's buffered line input, where COMMAND.COM waits for a command.
		if(0x21==inst.EvalUimm8() && 0x0A==GetAH())
		{
			++kairoLineInputs;
		}
		Interrupt(inst.EvalUimm8(),mem,2,2,true);
		EIPIncrement=0;
		break;
'''),
    ],
    'tsugaru/src/cpu/i486templatefunctions.h': [
        ('tsugaru/src/cpu/i486templatefunctions.h',
         '''
	if(nullptr!=accessPtr)
	{
		cpputil::PutWord(accessPtr,value);
		return;
	}

''',
         '''
	if(nullptr!=accessPtr)
	{
		mem.KairoWritten(accessPtr,2); // KairoTowns
		unsigned char before[2]; // KairoTowns
		std::memcpy(before,accessPtr,2);
		cpputil::PutWord(accessPtr,value);
		mem.KairoPushed(accessPtr,before,2);
		return;
	}

'''),
        ('tsugaru/src/cpu/i486templatefunctions.h',
         '''
	if(nullptr!=accessPtr)
	{
		cpputil::PutDword(accessPtr,value);
		return;
	}
	StoreDword(mem,addressSize,state.SS(),ESP,value);
''',
         '''
	if(nullptr!=accessPtr)
	{
		mem.KairoWritten(accessPtr,4); // KairoTowns
		unsigned char before[4]; // KairoTowns
		std::memcpy(before,accessPtr,4);
		cpputil::PutDword(accessPtr,value);
		mem.KairoPushed(accessPtr,before,4);
		return;
	}
	StoreDword(mem,addressSize,state.SS(),ESP,value);
'''),
        ('tsugaru/src/cpu/i486templatefunctions.h',
         '''	auto accessPtr=GetStackAccessPointer(mem,linearAddr,bytesToStore);
	if(nullptr!=accessPtr)
	{
		if(16==operandSize)
		{
			cpputil::PutWord(accessPtr+2,firstPush);
''',
         '''	auto accessPtr=GetStackAccessPointer(mem,linearAddr,bytesToStore);
	if(nullptr!=accessPtr)
	{
		mem.KairoWritten(accessPtr,bytesToStore); // KairoTowns
		unsigned char before[8]; // KairoTowns
		std::memcpy(before,accessPtr,bytesToStore);
		if(16==operandSize)
		{
			cpputil::PutWord(accessPtr+2,firstPush);
'''),
        ('tsugaru/src/cpu/i486templatefunctions.h',
         '''			cpputil::PutDword(accessPtr+4,firstPush);
			cpputil::PutDword(accessPtr  ,secondPush);
		}
		return;
	}

''',
         '''			cpputil::PutDword(accessPtr+4,firstPush);
			cpputil::PutDword(accessPtr  ,secondPush);
		}
		mem.KairoPushed(accessPtr,before,bytesToStore);
		return;
	}

'''),
        ('tsugaru/src/cpu/i486templatefunctions.h',
         '''	auto accessPtr=GetStackAccessPointer(mem,linearAddr,bytesToStore);
	if(nullptr!=accessPtr)
	{
		if(16==operandSize)
		{
			cpputil::PutWord(accessPtr+4,firstPush);
''',
         '''	auto accessPtr=GetStackAccessPointer(mem,linearAddr,bytesToStore);
	if(nullptr!=accessPtr)
	{
		mem.KairoWritten(accessPtr,bytesToStore); // KairoTowns
		unsigned char before[12]; // KairoTowns
		std::memcpy(before,accessPtr,bytesToStore);
		if(16==operandSize)
		{
			cpputil::PutWord(accessPtr+4,firstPush);
'''),
        ('tsugaru/src/cpu/i486templatefunctions.h',
         '''			cpputil::PutDword(accessPtr+4,secondPush);
			cpputil::PutDword(accessPtr  ,thirdPush);
		}
		return;
	}

''',
         '''			cpputil::PutDword(accessPtr+4,secondPush);
			cpputil::PutDword(accessPtr  ,thirdPush);
		}
		mem.KairoPushed(accessPtr,before,bytesToStore);
		return;
	}

'''),
    ],
    'tsugaru/src/diskdrive/diskdrive.cpp': [
        ('tsugaru/src/diskdrive/diskdrive.cpp',
         '''#include "vmbase.h"
#include "d77ext.h"



void DiskDrive::Sector::Make(unsigned int C,unsigned int H,unsigned int R,unsigned int N)
''',
         '''#include "vmbase.h"
#include "d77ext.h"

// KairoTowns: where a disk a save state names is now (shared kairo/state_paths.h).
extern "C" const char *kairo_state_path(const char *recorded);



void DiskDrive::Sector::Make(unsigned int C,unsigned int H,unsigned int R,unsigned int N)
'''),
        ('tsugaru/src/diskdrive/diskdrive.cpp',
         '''	for(auto &imgf : imgFile)
	{
		imgf.img.fileType=ReadUint32(data);
		auto fName=ReadString(data);
		std::string relPath;
		if(1<=version)
		{
''',
         '''	for(auto &imgf : imgFile)
	{
		imgf.img.fileType=ReadUint32(data);
		auto fName=std::string(kairo_state_path(ReadString(data).c_str())); // KairoTowns
		std::string relPath;
		if(1<=version)
		{
'''),
    ],
    'tsugaru/src/inout/inout.cpp': [
        ('tsugaru/src/inout/inout.cpp',
         '''
unsigned int InOut::In8(unsigned int port)
{
	unsigned int value=ioMap[port&IO_ADDR_MASK]->IOReadByte(port&IO_ADDR_MASK);

	// Read from appropriate device..
''',
         '''
unsigned int InOut::In8(unsigned int port)
{
	// KairoTowns
	if(nullptr==kairoPureRead || true!=kairoPureRead(kairoPureOwner,port&IO_ADDR_MASK,1))
	{
		++accesses;
	}
	unsigned int value=ioMap[port&IO_ADDR_MASK]->IOReadByte(port&IO_ADDR_MASK);

	// Read from appropriate device..
'''),
        ('tsugaru/src/inout/inout.cpp',
         '''
unsigned int InOut::In16(unsigned int port)
{
	unsigned int value=ioMap[port&IO_ADDR_MASK]->IOReadWord(port&IO_ADDR_MASK);

	if(true==takeLog)
''',
         '''
unsigned int InOut::In16(unsigned int port)
{
	// KairoTowns
	if(nullptr==kairoPureRead || true!=kairoPureRead(kairoPureOwner,port&IO_ADDR_MASK,2))
	{
		++accesses;
	}
	unsigned int value=ioMap[port&IO_ADDR_MASK]->IOReadWord(port&IO_ADDR_MASK);

	if(true==takeLog)
'''),
        ('tsugaru/src/inout/inout.cpp',
         '''
unsigned int InOut::In32(unsigned int port)
{
	unsigned int value=ioMap[port&IO_ADDR_MASK]->IOReadDword(port&IO_ADDR_MASK);

	if(true==takeLog)
''',
         '''
unsigned int InOut::In32(unsigned int port)
{
	// KairoTowns
	if(nullptr==kairoPureRead || true!=kairoPureRead(kairoPureOwner,port&IO_ADDR_MASK,4))
	{
		++accesses;
	}
	unsigned int value=ioMap[port&IO_ADDR_MASK]->IOReadDword(port&IO_ADDR_MASK);

	if(true==takeLog)
'''),
        ('tsugaru/src/inout/inout.cpp',
         '''
void InOut::Out8(unsigned int port,unsigned int value)
{
	ioMap[port&IO_ADDR_MASK]->IOWriteByte(port&IO_ADDR_MASK,value);

	if(true==takeLog)
''',
         '''
void InOut::Out8(unsigned int port,unsigned int value)
{
	++accesses; // KairoTowns
	ioMap[port&IO_ADDR_MASK]->IOWriteByte(port&IO_ADDR_MASK,value);

	if(true==takeLog)
'''),
        ('tsugaru/src/inout/inout.cpp',
         '''}
void InOut::Out16(unsigned int port,unsigned int value)
{
	ioMap[port&IO_ADDR_MASK]->IOWriteWord(port&IO_ADDR_MASK,value);

	if(true==takeLog)
''',
         '''}
void InOut::Out16(unsigned int port,unsigned int value)
{
	++accesses; // KairoTowns
	ioMap[port&IO_ADDR_MASK]->IOWriteWord(port&IO_ADDR_MASK,value);

	if(true==takeLog)
'''),
        ('tsugaru/src/inout/inout.cpp',
         '''}
void InOut::Out32(unsigned int port,unsigned int value)
{
	ioMap[port&IO_ADDR_MASK]->IOWriteDword(port&IO_ADDR_MASK,value);

	if(true==takeLog)
''',
         '''}
void InOut::Out32(unsigned int port,unsigned int value)
{
	++accesses; // KairoTowns
	ioMap[port&IO_ADDR_MASK]->IOWriteDword(port&IO_ADDR_MASK,value);

	if(true==takeLog)
'''),
    ],
    'tsugaru/src/inout/inout.h': [
        ('tsugaru/src/inout/inout.h',
         '''	void AddDevice(Device *devPtr,unsigned int minIOPort,unsigned int maxIOPort);
	void AddDevice(Device *devPtr,unsigned int ioPort);

	unsigned int In8(unsigned int port);
	unsigned int In16(unsigned int port);
	unsigned int In32(unsigned int port);
''',
         '''	void AddDevice(Device *devPtr,unsigned int minIOPort,unsigned int maxIOPort);
	void AddDevice(Device *devPtr,unsigned int ioPort);

	/*! KairoTowns: counts every In and Out but the reads kairoPureRead says have no effect.
	*/
	uint64_t accesses=0;

	/*! KairoTowns: asked before every In, with the port and its bytes: true when the read has no
	    effect on any device (the wait-loop skip then counts it apart from accesses, core/wait_loop.h).
	*/
	bool (*kairoPureRead)(void *owner,unsigned int port,unsigned int bytes)=nullptr;
	void *kairoPureOwner=nullptr;

	/*! KairoTowns: the device an In or Out of the port goes to, which also answers the other bytes
	    of a word or dword read of it.
	*/
	inline const Device *KairoDevice(unsigned int port) const
	{
		return ioMap[port&IO_ADDR_MASK];
	}

	unsigned int In8(unsigned int port);
	unsigned int In16(unsigned int port);
	unsigned int In32(unsigned int port);
'''),
    ],
    'tsugaru/src/ramrom/ramrom.cpp': [
        ('tsugaru/src/ramrom/ramrom.cpp',
         '''
void Memory::CleanUp(void)
{
	for(auto &ptr : memAccessPtr)
	{
		ptr=&nullAccess;
''',
         '''
void Memory::CleanUp(void)
{
	++kairoMapGeneration;
	for(auto &ptr : memAccessPtr)
	{
		ptr=&nullAccess;
'''),
        ('tsugaru/src/ramrom/ramrom.cpp',
         '''		std::cout << "       to integer multiple of 0x1000 minus 1." << std::endl;
		return;
	}
	auto low=physAddrLow>>GRANURALITY_SHIFT;
	auto high=physAddrHigh>>GRANURALITY_SHIFT;
	for(auto i=low; i<=high; ++i)
''',
         '''		std::cout << "       to integer multiple of 0x1000 minus 1." << std::endl;
		return;
	}
	++kairoMapGeneration;
	auto low=physAddrLow>>GRANURALITY_SHIFT;
	auto high=physAddrHigh>>GRANURALITY_SHIFT;
	for(auto i=low; i<=high; ++i)
'''),
        ('tsugaru/src/ramrom/ramrom.cpp',
         '''}
void Memory::RemoveAccess(unsigned int physAddrLow,unsigned int physAddrHigh)
{
	auto low=physAddrLow>>GRANURALITY_SHIFT;
	auto high=physAddrHigh>>GRANURALITY_SHIFT;
	for(auto i=low; i<=high; ++i)
''',
         '''}
void Memory::RemoveAccess(unsigned int physAddrLow,unsigned int physAddrHigh)
{
	++kairoMapGeneration;
	auto low=physAddrLow>>GRANURALITY_SHIFT;
	auto high=physAddrHigh>>GRANURALITY_SHIFT;
	for(auto i=low; i<=high; ++i)
'''),
        ('tsugaru/src/ramrom/ramrom.cpp',
         '''}
void Memory::SetAccessObject(MemoryAccess *memAccess,unsigned int physAddr)
{
	auto slot=physAddr>>GRANURALITY_SHIFT;
	memAccessPtr[slot]=memAccess;
}
''',
         '''}
void Memory::SetAccessObject(MemoryAccess *memAccess,unsigned int physAddr)
{
	++kairoMapGeneration;
	auto slot=physAddr>>GRANURALITY_SHIFT;
	memAccessPtr[slot]=memAccess;
}
'''),
    ],
    'tsugaru/src/ramrom/ramrom.h': [
        ('tsugaru/src/ramrom/ramrom.h',
         '''


#include <vector>
#include "cpputil.h"

''',
         '''


#include <cstddef>
#include <cstring>
#include <vector>
#include "cpputil.h"

'''),
        ('tsugaru/src/ramrom/ramrom.h',
         '''	*/
	MemoryAccess *memAccessChain=nullptr;

	virtual unsigned int FetchByte(unsigned int physAddr) const=0;
	virtual unsigned int FetchWord(unsigned int physAddr) const;
	virtual unsigned int FetchDword(unsigned int physAddr) const;
''',
         '''	*/
	MemoryAccess *memAccessChain=nullptr;

	/*! KairoTowns: true for an object whose reads have no effect (main RAM). Memory counts reads
	    from every other object in sideEffects.
	*/
	bool readWithoutSideEffect=false;

	virtual unsigned int FetchByte(unsigned int physAddr) const=0;
	virtual unsigned int FetchWord(unsigned int physAddr) const;
	virtual unsigned int FetchDword(unsigned int physAddr) const;
'''),
        ('tsugaru/src/ramrom/ramrom.h',
         '''			return (addr&~(MEMORY_WINDOW_SIZE-1))==_linearBaseAddr;
		}

		/*! Returns a memory-access pointer from this window.
		    In this case, physical or linear doesn't matter.
		    Higher than 12th bit will be masked anyway.
''',
         '''			return (addr&~(MEMORY_WINDOW_SIZE-1))==_linearBaseAddr;
		}

		/*! KairoTowns: where _linearBaseAddr is, for the JIT's translated code (core/jit). */
		static size_t KairoLinearBaseAddrOffset(void)
		{
			return offsetof(ConstMemoryWindow,_linearBaseAddr);
		}

		/*! Returns a memory-access pointer from this window.
		    In this case, physical or linear doesn't matter.
		    Higher than 12th bit will be masked anyway.
'''),
        ('tsugaru/src/ramrom/ramrom.h',
         '''		{
			return (addr&~(MEMORY_WINDOW_SIZE-1))==linearBaseAddr;
		}
	};

	virtual ConstMemoryWindow GetConstMemoryWindow(unsigned int physAddr) const;
''',
         '''		{
			return (addr&~(MEMORY_WINDOW_SIZE-1))==linearBaseAddr;
		}

		/*! KairoTowns: where linearBaseAddr is, for the JIT's translated code (core/jit). */
		static size_t KairoLinearBaseAddrOffset(void)
		{
			return offsetof(MemoryWindow,linearBaseAddr);
		}
	};

	virtual ConstMemoryWindow GetConstMemoryWindow(unsigned int physAddr) const;
'''),
        ('tsugaru/src/ramrom/ramrom.h',
         '''	};

public:
	enum
	{
		MEMORY_ACCESS_SLOT_SIZE=(1<<GRANURALITY_SHIFT),
''',
         '''	};

public:
	/*! KairoTowns: counts every store and every read from an object whose reads may have an
	    effect (readWithoutSideEffect false), including stores the CPU makes through memory
	    windows, and every push through a stack pointer that changes memory (KairoPushed). While
	    it stays the same, the CPU has only read RAM and pushed what the stack held.
	*/
	mutable uint64_t sideEffects=0;

	/*! KairoTowns: where memory is written, for the JIT (core/jit), which checks translated
	    code only where it may have changed. Main RAM's bytes (kairoRAM, the main RAM object's
	    state, indexed by physical address) count every store into them in kairoLines, by line of
	    1<<KAIRO_LINE_SHIFT bytes: the line of a store's first byte, and every line of a longer
	    push. A store the CPU makes through a window or pointer elsewhere counts in kairoWrites,
	    and a store Memory makes into any other object in kairoOtherWrites. The JIT sets
	    kairoRAM, kairoRAMBytes and kairoLines; without it, every store counts in kairoWrites or
	    kairoOtherWrites.
	*/
	enum
	{
		KAIRO_LINE_SHIFT=8
	};
	const unsigned char *kairoRAM=nullptr;
	uint64_t kairoRAMBytes=0;
	uint64_t *kairoLines=nullptr;
	uint64_t kairoWrites=0,kairoOtherWrites=0;
	inline void KairoWrittenRAM(unsigned int physAddr)
	{
		if(nullptr!=kairoLines && physAddr<kairoRAMBytes)
		{
			++kairoLines[physAddr>>KAIRO_LINE_SHIFT];
		}
		else
		{
			++kairoWrites;
		}
	}
	inline void KairoWritten(const unsigned char *at,unsigned int bytes)
	{
		const uint64_t offset=uint64_t(reinterpret_cast<uintptr_t>(at)-reinterpret_cast<uintptr_t>(kairoRAM));
		if(nullptr!=kairoLines && offset<kairoRAMBytes && offset+bytes<=kairoRAMBytes)
		{
			for(uint64_t line=offset>>KAIRO_LINE_SHIFT; line<=((offset+bytes-1)>>KAIRO_LINE_SHIFT); ++line)
			{
				++kairoLines[line];
			}
		}
		else
		{
			++kairoWrites;
		}
	}

	/*! KairoTowns: a push the CPU made through a stack pointer into a memory window, which bypasses
	    Memory: counts in sideEffects when the [bytes] bytes at [at] differ from what they were
	    ([before]). A push of what the stack already holds changes nothing.
	*/
	inline void KairoPushed(const unsigned char *at,const unsigned char *before,unsigned int bytes)
	{
		if(0!=std::memcmp(at,before,bytes))
		{
			++sideEffects;
		}
	}

	/*! KairoTowns: counts changes to which memory-access object serves which 4KB slot, for the
	    JIT (core/jit), which keeps where main RAM is per page.
	*/
	uint64_t kairoMapGeneration=0;

	enum
	{
		MEMORY_ACCESS_SLOT_SIZE=(1<<GRANURALITY_SHIFT),
'''),
        ('tsugaru/src/ramrom/ramrom.h',
         '''	inline unsigned int FetchByte(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		return memAccess->FetchByte(physAddr);
	}

	inline unsigned int FetchWord(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		return memAccess->FetchWord(physAddr);
	}

	inline unsigned int FetchDword(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		return memAccess->FetchDword(physAddr);
	}

''',
         '''	inline unsigned int FetchByte(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		if(true!=memAccess->readWithoutSideEffect)
		{
			++sideEffects;
		}
		return memAccess->FetchByte(physAddr);
	}

	inline unsigned int FetchWord(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		if(true!=memAccess->readWithoutSideEffect)
		{
			++sideEffects;
		}
		return memAccess->FetchWord(physAddr);
	}

	inline unsigned int FetchDword(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		if(true!=memAccess->readWithoutSideEffect)
		{
			++sideEffects;
		}
		return memAccess->FetchDword(physAddr);
	}

'''),
        ('tsugaru/src/ramrom/ramrom.h',
         '''		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		return memAccess->GetConstMemoryWindow(physAddr);
	}
	inline MemoryAccess::MemoryWindow GetMemoryWindow(unsigned int physAddr)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
''',
         '''		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		return memAccess->GetConstMemoryWindow(physAddr);
	}
	/*! KairoTowns: the memory-access object of every 4KB slot, for the JIT's translated code
	    (core/jit), which reads main RAM directly where Memory would.
	*/
	inline MemoryAccess *const *KairoAccessTable(void) const
	{
		return memAccessPtr.data();
	}

	inline MemoryAccess::MemoryWindow GetMemoryWindow(unsigned int physAddr)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
'''),
        ('tsugaru/src/ramrom/ramrom.h',
         '''	inline void StoreByte(unsigned int physAddr,unsigned char data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		memAccess->StoreByte(physAddr,data);
	}

	inline unsigned int FetchByteDMA(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		return memAccess->FetchByteDMA(physAddr);
	}

	inline void StoreByteDMA(unsigned int physAddr,unsigned char data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		memAccess->StoreByteDMA(physAddr,data);
	}

	inline void StoreWord(unsigned int physAddr,unsigned int data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		memAccess->StoreWord(physAddr,data);
	}

	inline void StoreDword(unsigned int physAddr,unsigned int data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		memAccess->StoreDword(physAddr,data);
	}
};
''',
         '''	inline void StoreByte(unsigned int physAddr,unsigned char data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		++sideEffects;
		if(true==memAccess->readWithoutSideEffect)
		{
			KairoWrittenRAM(physAddr);
		}
		else
		{
			++kairoOtherWrites;
		}
		memAccess->StoreByte(physAddr,data);
	}

	inline unsigned int FetchByteDMA(unsigned int physAddr) const
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		++sideEffects;
		return memAccess->FetchByteDMA(physAddr);
	}

	inline void StoreByteDMA(unsigned int physAddr,unsigned char data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		++sideEffects;
		if(true==memAccess->readWithoutSideEffect)
		{
			KairoWrittenRAM(physAddr);
		}
		else
		{
			++kairoOtherWrites;
		}
		memAccess->StoreByteDMA(physAddr,data);
	}

	inline void StoreWord(unsigned int physAddr,unsigned int data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		++sideEffects;
		if(true==memAccess->readWithoutSideEffect)
		{
			KairoWrittenRAM(physAddr);
		}
		else
		{
			++kairoOtherWrites;
		}
		memAccess->StoreWord(physAddr,data);
	}

	inline void StoreDword(unsigned int physAddr,unsigned int data)
	{
		auto memAccess=memAccessPtr[physAddr>>GRANURALITY_SHIFT];
		++sideEffects;
		if(true==memAccess->readWithoutSideEffect)
		{
			KairoWrittenRAM(physAddr);
		}
		else
		{
			++kairoOtherWrites;
		}
		memAccess->StoreDword(physAddr,data);
	}
};
'''),
    ],
    'tsugaru/src/towns/cdrom/cdrom.cpp': [
        ('tsugaru/src/towns/cdrom/cdrom.cpp',
         '''#include "towns.h"
#include "cpputil.h"

TownsCDROM::AsyncWaveReader::AsyncWaveReader()
{
}
''',
         '''#include "towns.h"
#include "cpputil.h"

// KairoTowns: where a disk a save state names is now (shared kairo/state_paths.h).
extern "C" const char *kairo_state_path(const char *recorded);

TownsCDROM::AsyncWaveReader::AsyncWaveReader()
{
}
'''),
        ('tsugaru/src/towns/cdrom/cdrom.cpp',
         '''	msfEnd-=offset;

	waveReader.Start(&state.GetDisc(),msfBegin,msfEnd);
}

void TownsCDROM::DelayedCommandExecution(unsigned long long int townsTime)
''',
         '''	msfEnd-=offset;

	waveReader.Start(&state.GetDisc(),msfBegin,msfEnd);
	while(true==waitForWave && AsyncWaveReader::STATE_BUSY==waveReader.GetState()) // KairoTowns
	{
		std::this_thread::yield();
	}
}

void TownsCDROM::DelayedCommandExecution(unsigned long long int townsTime)
'''),
        ('tsugaru/src/towns/cdrom/cdrom.cpp',
         '''


	std::string fName=ReadString(data);
	std::string relPath;
	if(1<=version)
	{
''',
         '''


	std::string fName=std::string(kairo_state_path(ReadString(data).c_str())); // KairoTowns
	std::string relPath;
	if(1<=version)
	{
'''),
    ],
    'tsugaru/src/towns/cdrom/cdrom.h': [
        ('tsugaru/src/towns/cdrom/cdrom.h',
         '''		void ThreadFunc(void);
	};
	AsyncWaveReader waveReader;

	class State
	{
''',
         '''		void ThreadFunc(void);
	};
	AsyncWaveReader waveReader;
	/*! KairoTowns: when true, a CD audio read finishes before the guest goes on, so whether the
	    audio is ready when the guest asks no longer depends on the host's disk; for tests that
	    compare two runs.
	*/
	bool waitForWave=false;

	class State
	{
'''),
    ],
    'tsugaru/src/towns/memory/physmem.cpp': [
        ('tsugaru/src/towns/memory/physmem.cpp',
         '''		return state.nativeVRAMMask[(state.nativeVRAMMaskRegisterLatch<<1)+1];

	case TOWNSIO_KANJI_JISCODE_HIGH://  0xFF94,
		break;
	case TOWNSIO_KANJI_JISCODE_LOW://   0xFF95,
		break;
	case TOWNSIO_KANJI_PTN_HIGH://      0xFF96,
		{
			auto ROMCode=(state.kanjiROMAccess.FontROMCode())&8191;
''',
         '''		return state.nativeVRAMMask[(state.nativeVRAMMaskRegisterLatch<<1)+1];

	case TOWNSIO_KANJI_JISCODE_HIGH://  0xFF94,
		// KairoTowns: data was returned uninitialized; as the memory-mapped port at CFF94 reads.
		return 0x80;
	case TOWNSIO_KANJI_JISCODE_LOW://   0xFF95,
		// KairoTowns: data was returned uninitialized; as the memory-mapped port at CFF95 reads.
		return 0xff;
	case TOWNSIO_KANJI_PTN_HIGH://      0xFF96,
		{
			auto ROMCode=(state.kanjiROMAccess.FontROMCode())&8191;
'''),
        ('tsugaru/src/towns/memory/physmem.cpp',
         '''		}
		break;
	case TOWNSIO_KVRAM_OR_ANKFONT://    0xFF99,
		break;

	case TOWNSIO_MEMCARD_STATUS: //           0x48A, // [2] pp.93
		data=(true==state.memCard.changed ? 0x80 : 0);
''',
         '''		}
		break;
	case TOWNSIO_KVRAM_OR_ANKFONT://    0xFF99,
		// KairoTowns: data was returned uninitialized; as the memory-mapped port at CFF99 reads.
		return 0xff;

	case TOWNSIO_MEMCARD_STATUS: //           0x48A, // [2] pp.93
		data=(true==state.memCard.changed ? 0x80 : 0);
'''),
        ('tsugaru/src/towns/memory/physmem.cpp',
         '''
	mainRAMAccess.SetPhysicalMemoryPointer(this);
	mainRAMAccess.SetCPUPointer(&cpu);
	mem.AddAccess(&mainRAMAccess,0x00000000,0x000FFFFF);

	FMRVRAMAccess.SetPhysicalMemoryPointer(this);
''',
         '''
	mainRAMAccess.SetPhysicalMemoryPointer(this);
	mainRAMAccess.SetCPUPointer(&cpu);
	mainRAMAccess.readWithoutSideEffect=true; // KairoTowns: see MemoryAccess::readWithoutSideEffect.
	mem.AddAccess(&mainRAMAccess,0x00000000,0x000FFFFF);

	FMRVRAMAccess.SetPhysicalMemoryPointer(this);
'''),
    ],
    'tsugaru/src/towns/pic/pic.cpp': [
        ('tsugaru/src/towns/pic/pic.cpp',
         '''			break;
		}
	}
}
bool TownsPIC::I8259A::WriteReg1(unsigned char data)
{
	if(0==init_stage)
	{
		OCW[0]=data;
''',
         '''			break;
		}
	}
	owner->state.UpdateIRRCache(); // KairoTowns: ISR and the priority decide what fires.
}
bool TownsPIC::I8259A::WriteReg1(unsigned char data)
{
	bool ICW4Written=false;
	if(0==init_stage)
	{
		OCW[0]=data;
'''),
        ('tsugaru/src/towns/pic/pic.cpp',
         '''	{
		ICW[init_stage++]=data;
		init_stage&=3;
		if(0==init_stage)
		{
			return true;
		}
	}
	return false;
}
bool TownsPIC::I8259A::PollingMode(void) const
{
''',
         '''	{
		ICW[init_stage++]=data;
		init_stage&=3;
		ICW4Written=(0==init_stage);
	}
	owner->state.UpdateIRRCache(); // KairoTowns: the mask and ICW4 decide what fires.
	return ICW4Written;
}
bool TownsPIC::I8259A::PollingMode(void) const
{
'''),
        ('tsugaru/src/towns/pic/pic.cpp',
         '''{
	// [2] pp.58
	unsigned int bit=(1<<intNum);
	if(true==request)
	{
		if(0==(IRR&bit) && TriggerMode()==TRIGGER_EDGE)
''',
         '''{
	// [2] pp.58
	unsigned int bit=(1<<intNum);
	const auto before=IRR;
	if(true==request)
	{
		if(0==(IRR&bit) && TriggerMode()==TRIGGER_EDGE)
'''),
        ('tsugaru/src/towns/pic/pic.cpp',
         '''	{
		IRR&=(~bit);
	}
	owner->state.UpdateIRRCache();
}

bool TownsPIC::I8259A::GetInterruptRequestBit(unsigned int intNum) const
''',
         '''	{
		IRR&=(~bit);
	}
	// KairoTowns: devices restate their requests at every polling; only a change can change
	// what fires.
	if(before!=IRR)
	{
		owner->state.UpdateIRRCache();
	}
}

bool TownsPIC::I8259A::GetInterruptRequestBit(unsigned int intNum) const
'''),
        ('tsugaru/src/towns/pic/pic.cpp',
         '''	else
	{
		ISR|=(1<<INTToGo);
		auto CPUINT=(GetT()&0xF8)|(INTToGo&7);
		cpu.RedirectInterrupt(CPUINT,mem,0,0,false);
	}
''',
         '''	else
	{
		ISR|=(1<<INTToGo);
		owner->state.UpdateIRRCache(); // KairoTowns: an interrupt in service holds others back.
		auto CPUINT=(GetT()&0xF8)|(INTToGo&7);
		cpu.RedirectInterrupt(CPUINT,mem,0,0,false);
	}
'''),
    ],
    'tsugaru/src/towns/pic/pic.h': [
        ('tsugaru/src/towns/pic/pic.h',
         '''		I8259A i8259A[2];

		mutable unsigned char IRR0_OR_IRR1_Cache=0; // Not saved in the state file.  Updated after state is loaded.
		void UpdateIRRCache(void) const
		{
			IRR0_OR_IRR1_Cache=(i8259A[0].IRR|i8259A[1].IRR);
		}

		void Reset(void);
''',
         '''		I8259A i8259A[2];

		mutable unsigned char IRR0_OR_IRR1_Cache=0; // Not saved in the state file.  Updated after state is loaded.
		// KairoTowns: non-zero only while ProcessIRQ would fire an interrupt once the CPU accepts
		// one: a request that is not masked (OCW1), not held back by one in service (ISR) and not
		// held back in special fully nested mode, chosen exactly as ProcessIRQ chooses. A request
		// that cannot fire no longer makes ProcessIRQ search both chips after every instruction
		// (KairoTowns#8). Called after every change to the chips' state: requests, register
		// writes, a fired interrupt, a reset and a loaded state.
		void UpdateIRRCache(void) const
		{
			unsigned int chip=0,INTToGo=8;
			if(0==(i8259A[0].OCW[0]&0x80))
			{
				if(7==i8259A[0].highestPriorityInt)
				{
					chip=1;
				}
				INTToGo=i8259A[chip].INTToGo();
				if(8<=INTToGo && 0==i8259A[chip].ISR)
				{
					chip=1-chip;
					INTToGo=i8259A[chip].INTToGo();
				}
			}
			else
			{
				INTToGo=i8259A[chip].INTToGo();
			}
			IRR0_OR_IRR1_Cache=(INTToGo<8 &&
				!(i8259A[chip].SpecialFullyNestedMode() && i8259A[chip].HigherPriorityINTIsInService(INTToGo))) ? 1 : 0;
		}

		void Reset(void);
'''),
    ],
    'tsugaru/src/towns/render/render.cpp': [
        ('tsugaru/src/towns/render/render.cpp',
         '''
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

<< LICENSE */
#include <cstring>
#include "cpputil.h"
#include "render.h"



TownsRender::TownsRender()
''',
         '''
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

<< LICENSE */
#include <algorithm>
#include <cstring>
#include <type_traits>
#include "cpputil.h"
#include "render.h"



TownsRender::TownsRender()
'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''void TownsRender::OerrideShowPage(bool layer0,bool layer1)
{
	crtcShowPage[0]=layer0;
	crtcShowPage[1]=layer1;
}

void TownsRender::BuildImage(const unsigned char VRAM[],const TownsCRTC::AnalogPalette &palette,const TownsCRTC::ChaseHQPalette &chaseHQPalette)
{
	SetResolution(crtcRenderSize.x(),crtcRenderSize.y());

	std::memset(rgba.data(),0,rgba.size());

''',
         '''void TownsRender::OerrideShowPage(bool layer0,bool layer1)
{
	crtcShowPage[0]=layer0;
	crtcShowPage[1]=layer1;
}

size_t TownsRender::KairoVRAMBytesRead(size_t vramSize) const
{
	// The high-resolution CRTC's layers start at 80000H a page and wrap in all of VRAM.
	if(true==highResCRTC)
	{
		return vramSize;
	}
	size_t end=0;
	// A layer's row starts at layer.VRAMAddr+layer.VRAMHSkipBytes plus an offset no higher than
	// highestOffset, and reads at most its pixels across the screen from there (no layer is
	// zoomed below 1x), plus the rest of a dword.
	auto reach=[&](const TownsCRTC::Layer &layer,size_t highestOffset)
	{
		const size_t row=size_t(layer.sizeOnMonitor.x())*layer.bitsPerPixel/8+4;
		end=std::max(end,size_t(layer.VRAMAddr)+layer.VRAMHSkipBytes+highestOffset+row);
	};
	if(true==crtcIsSinglePageMode)
	{
		// VRAM1Trans takes every offset below 80000H.
		if(true==crtcShowPage[0])
		{
			reach(crtcLayer[0],0x7FFFF);
		}
	}
	else
	{
		// VRAM0Trans leaves the offset, which the layer's mask keeps within it.
		for(int page=0; page<2; ++page)
		{
			if(true==crtcShowPage[page])
			{
				reach(crtcLayer[page],crtcLayer[page].VScrollMask);
			}
		}
	}
	return std::min(end,vramSize);
}

void TownsRender::BuildImage(const unsigned char VRAM[],const TownsCRTC::AnalogPalette &palette,const TownsCRTC::ChaseHQPalette &chaseHQPalette)
{
	SetResolution(crtcRenderSize.x(),crtcRenderSize.y());

	std::memset(rgba.data(),0,rgba.size());

'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''	Image img;
	img.wid=wid;
	img.hei=hei;
	img.rgba=this->rgba.data();
	return img;
}
TownsRender::ImageCopy TownsRender::MoveImage(void)
{
	ImageCopy img;
	img.wid=this->wid;
	img.hei=this->hei;
	std::swap(img.rgba,this->rgba);
''',
         '''	Image img;
	img.wid=wid;
	img.hei=hei;
	img.rgba=this->rgba.data();
	return img;
}
void TownsRender::SwapImage(ImageCopy &img)
{
	std::swap(img.rgba,this->rgba);
	std::swap(img.wid,this->wid);
	std::swap(img.hei,this->hei);
}
TownsRender::ImageCopy TownsRender::MoveImage(void)
{
	ImageCopy img;
	img.wid=this->wid;
	img.hei=this->hei;
	std::swap(img.rgba,this->rgba);
'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''				const int X = layer.originOnMonitor.x();
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0]; ++i)
					{
						std::memcpy(dst, &palette[pix], 4);
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1]; ++i)
					{
						std::memcpy(dst, &palette[pix], 4);
						dst += 4;
					}
					++src;
				}
''',
         '''				const int X = layer.originOnMonitor.x();
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				// KairoTowns: a zoomed VRAM byte can reach past the row's right edge; clip its pixels there.
				const unsigned char* dstEnd = rgba.data() + 4 * (Y + 1) * this->wid;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0] && dst < dstEnd; ++i)
					{
						std::memcpy(dst, &palette[pix], 4);
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1] && dst < dstEnd; ++i)
					{
						std::memcpy(dst, &palette[pix], 4);
						dst += 4;
					}
					++src;
				}
'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''				const int X = layer.originOnMonitor.x();
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0]; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &palette[pix], 4);
						}
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1]; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &palette[pix], 4);
						}
						dst += 4;
''',
         '''				const int X = layer.originOnMonitor.x();
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				// KairoTowns: a zoomed VRAM byte can reach past the row's right edge; clip its pixels there.
				const unsigned char* dstEnd = rgba.data() + 4 * (Y + 1) * this->wid;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0] && dst < dstEnd; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &palette[pix], 4);
						}
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1] && dst < dstEnd; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &palette[pix], 4);
						}
						dst += 4;
'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0]; ++i)
					{
						std::memcpy(dst, &paletteUpdate[pix], 4);
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1]; ++i)
					{
						std::memcpy(dst, &paletteUpdate[pix], 4);
						dst += 4;
					}
					++src;
				}
''',
         '''
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				// KairoTowns: a zoomed VRAM byte can reach past the row's right edge; clip its pixels there.
				const unsigned char* dstEnd = rgba.data() + 4 * (Y + 1) * this->wid;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0] && dst < dstEnd; ++i)
					{
						std::memcpy(dst, &paletteUpdate[pix], 4);
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1] && dst < dstEnd; ++i)
					{
						std::memcpy(dst, &paletteUpdate[pix], 4);
						dst += 4;
					}
					++src;
				}
'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0]; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &paletteUpdate[pix], 4);
						}
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1]; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &paletteUpdate[pix], 4);
						}
						dst += 4;
''',
         '''
				unsigned int VRAMAddr = (bytesPerLineTimesVRAMy & layer.VScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);
				const unsigned char* src = VRAMTop + VRAMAddr;
				unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
				auto dst = dstLine;
				// KairoTowns: a zoomed VRAM byte can reach past the row's right edge; clip its pixels there.
				const unsigned char* dstEnd = rgba.data() + 4 * (Y + 1) * this->wid;
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid; x += ZH[2])
				{
					unsigned char vrambyte = ((*src) & pixelMask);
					unsigned char pix = (vrambyte & 0x0f);
					for (int i = 0; i < ZH[0] && dst < dstEnd; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &paletteUpdate[pix], 4);
						}
						dst += 4;
					}
					pix = (vrambyte & 0xf0) >> 4;
					for (int i = 0; i < ZH[1] && dst < dstEnd; ++i)
					{
						if (0 != pix)
						{
							std::memcpy(dst, &paletteUpdate[pix], 4);
						}
						dst += 4;
'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''	const unsigned int VRAMVScrollMask=layer.VScrollMask;
	unsigned int lineVRAMOffset=0;
	const int ZHsrc[2]={layer.zoom2x.x()/2,(layer.zoom2x.x()+1)/2};  // For x.5 times zoom rate.
	auto ZV=layer.zoom2x.y()/2;

	auto bottomY=this->hei-ZV;
	if (true != transparent)
	{
		for (int y = 0; y < layer.sizeOnMonitor.y() && y + layer.originOnMonitor.y() <= bottomY; y += ZV)
		{
			auto X = layer.originOnMonitor.x();
			auto Y = y + layer.originOnMonitor.y();
			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
			for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

''',
         '''	const unsigned int VRAMVScrollMask=layer.VScrollMask;
	unsigned int lineVRAMOffset=0;
	const int ZHsrc[2]={layer.zoom2x.x()/2,(layer.zoom2x.x()+1)/2};  // For x.5 times zoom rate.
	auto ZV=layer.zoom2x.y()/2;

	auto bottomY=this->hei-ZV;

	// KairoTowns: a row whose VRAM is contiguous before the layer's offset translation (no
	// horizontal or vertical scroll wrap) is drawn one VRAM pixel at a time: each is read and
	// looked up once and written zoom times, exactly as the general loop below writes it pixel by
	// pixel. Returns false for a row the general loop draws.
	const auto powerOfTwoMask=[](uint32_t mask){return 0==(mask&(mask+1));};
	const bool fastRows=1<=ZHsrc[0] && 1<=ZHsrc[1] && powerOfTwoMask(VRAMHScrollMask) &&
	                    powerOfTwoMask(VRAMVScrollMask) && 0<=layer.originOnMonitor.x();
	const auto drawRow=[&](unsigned char *&dst,bool transparentRow) -> bool
	{
		const unsigned int inLineStart=layer.VRAMHSkipBytes;
		const unsigned int inLineLimit=layer.bytesPerLine;
		if(true!=fastRows || inLineLimit<=inLineStart)
		{
			return false;
		}
		const int xEnd=std::min<int>(layer.sizeOnMonitor.x(),(int)this->wid-(int)layer.originOnMonitor.x());
		if(xEnd<=0)
		{
			return false;
		}
		// The VRAM pixels the row reads: as many as its xEnd pixels take at ZHsrc[0], ZHsrc[1],
		// ZHsrc[0], ... each, and none at or past inLineLimit.
		const uint64_t pair=ZHsrc[0]+ZHsrc[1];
		const uint64_t pairs=xEnd/pair,rest=xEnd%pair;
		const uint64_t sources=2*pairs+(0==rest ? 0 : (rest<=(uint64_t)ZHsrc[0] ? 1 : 2));
		const uint64_t span=std::min<uint64_t>(sources-1,inLineLimit-1-inLineStart);
		const uint32_t h0=(inLineStart+VRAMOffsetHorizontal)&VRAMHScrollMask;
		if(VRAMHScrollMask<(uint64_t)h0+span)
		{
			return false;
		}
		const uint32_t v0=(lineVRAMOffset+h0+VRAMOffsetVertical)&VRAMVScrollMask;
		if(VRAMVScrollMask<(uint64_t)v0+span)
		{
			return false;
		}
		unsigned int address=VRAMBase+v0;
		unsigned int inLine=inLineStart;
		int ZHswitch=0;
		for(int x=0; x<xEnd && inLine<inLineLimit;)
		{
			unsigned int VRAMAddr=address;
			OFFSETTRANS::Trans(VRAMAddr);
			const unsigned char col8=VRAM[VRAMAddr];
			int n=ZHsrc[ZHswitch];
			const bool whole=(n<=xEnd-x);
			if(true!=whole)
			{
				n=xEnd-x;
			}
			if(true==transparentRow && 0==col8)
			{
				dst+=4*n;
			}
			else
			{
				for(int i=0; i<n; ++i)
				{
					std::memcpy(dst,&palette[col8],4);
					dst+=4;
				}
			}
			x+=n;
			if(true!=whole)
			{
				break;
			}
			ZHswitch=1-ZHswitch;
			++inLine;
			++address;
		}
		return true;
	};
	if (true != transparent)
	{
		for (int y = 0; y < layer.sizeOnMonitor.y() && y + layer.originOnMonitor.y() <= bottomY; y += ZV)
		{
			auto X = layer.originOnMonitor.x();
			auto Y = y + layer.originOnMonitor.y();
			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
			if(true!=drawRow(dst,false))
			for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
			for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

''',
         '''			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
			if(true!=drawRow(dst,true))
			for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''	// yStep should be 1 if transparent.
	// If transparnet==true, there is a possibility that memcpy overwrites background pixels.
	unsigned int yStep=(true!=transparent ? ZV0 : 1);
	auto bottomY=this->hei-yStep;
	auto X=layer.originOnMonitor.x();
	auto RenderOffsetX=X*2;
	if (true != transparent)
	{
		for (int y = 0; y < layer.sizeOnMonitor.y() && y + layer.originOnMonitor.y() <= bottomY; y += yStep)
		{
			auto Y = y + layer.originOnMonitor.y();
			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
			for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine + RenderOffsetX; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

				unsigned short col16 = cpputil::GetWord(VRAM + VRAMAddr);
''',
         '''	// yStep should be 1 if transparent.
	// If transparnet==true, there is a possibility that memcpy overwrites background pixels.
	unsigned int yStep=(true!=transparent ? ZV0 : 1);
	auto bottomY=this->hei-yStep;
	auto X=layer.originOnMonitor.x();
	auto RenderOffsetX=X*2;

	// KairoTowns: a row whose VRAM is contiguous (no horizontal or vertical scroll wrap) is drawn
	// one VRAM pixel at a time: each is read and converted once and written zoom times, exactly as
	// the general loop below writes it pixel by pixel. Returns false for a row the general loop draws.
	const auto powerOfTwoMask=[](uint32_t mask){return 0==(mask&(mask+1));};
	const bool fastRows=std::is_same<OFFSETTRANS,VRAM0Trans>::value && 1<=ZHsrc[0] && 1<=ZHsrc[1] &&
	                    powerOfTwoMask(VRAMHScrollMask) && powerOfTwoMask(VRAMVScrollMask) && 0<=X;
	const auto drawRow=[&](unsigned char *&dst,bool transparentRow) -> bool
	{
		const unsigned int inLineStart=layer.VRAMHSkipBytes;
		const unsigned int inLineLimit=layer.bytesPerLine+RenderOffsetX;
		if(true!=fastRows || inLineLimit<=inLineStart)
		{
			return false;
		}
		const int xEnd=std::min<int>(layer.sizeOnMonitor.x(),(int)this->wid-(int)layer.originOnMonitor.x());
		if(xEnd<=0)
		{
			return false;
		}
		// The VRAM pixels the row reads: as many as its xEnd pixels take at ZHsrc[0], ZHsrc[1],
		// ZHsrc[0], ... each, and none at or past inLineLimit.
		const uint64_t pair=ZHsrc[0]+ZHsrc[1];
		const uint64_t pairs=xEnd/pair,rest=xEnd%pair;
		const uint64_t sources=2*pairs+(0==rest ? 0 : (rest<=(uint64_t)ZHsrc[0] ? 1 : 2));
		const uint64_t spanByWidth=2*(sources-1);
		const uint64_t spanByLimit=2*(uint64_t)((inLineLimit-1-inLineStart)/2);
		const uint64_t span=std::min(spanByWidth,spanByLimit);
		const uint32_t h0=(inLineStart+VRAMOffsetHorizontal)&VRAMHScrollMask;
		if(VRAMHScrollMask<(uint64_t)h0+span)
		{
			return false;
		}
		const uint32_t v0=(lineVRAMOffset+h0+VRAMOffsetVertical)&VRAMVScrollMask;
		if(VRAMVScrollMask<(uint64_t)v0+span)
		{
			return false;
		}
		const unsigned char *src=VRAM+(unsigned int)(VRAMBase+v0);
		unsigned int inLine=inLineStart;
		int ZHswitch=0;
		if(ZHsrc[0]==ZHsrc[1])
		{
			// KairoTowns: a whole-number zoom, as the loop below draws it: each VRAM pixel z
			// times while it fits, then the part of one that does not, each written as whole
			// words.
			const int z=ZHsrc[0];
			const uint64_t sourcesLeft=(inLineLimit-inLineStart+1)/2;
			const uint64_t whole=std::min<uint64_t>(xEnd/z,sourcesLeft);
			const auto convert=[](unsigned short col16) -> uint32_t
			{
				return (uint32_t)(((col16>>2)&0xf8)|((col16>>7)&7))|
				       ((uint32_t)(((col16>>7)&0xf8)|((col16>>12)&7))<<8)|
				       ((uint32_t)(((col16<<3)&0xf8)|((col16>>2)&7))<<16)|0xff000000u;
			};
			for(uint64_t k=0; k<whole; ++k)
			{
				const unsigned short col16=cpputil::GetWord(src+2*k);
				if(true!=transparentRow || 0==(col16&0x8000))
				{
					const uint32_t pixel=convert(col16);
					if(2==z)
					{
						const uint64_t two=pixel|((uint64_t)pixel<<32);
						std::memcpy(dst,&two,8);
					}
					else
					{
						for(int i=0; i<z; ++i)
						{
							std::memcpy(dst+4*i,&pixel,4);
						}
					}
				}
				dst+=4*z;
			}
			const int x=(int)(whole*z);
			if(x<xEnd && whole<sourcesLeft)
			{
				const unsigned short col16=cpputil::GetWord(src+2*whole);
				const int n=xEnd-x;
				if(true!=transparentRow || 0==(col16&0x8000))
				{
					const uint32_t pixel=convert(col16);
					for(int i=0; i<n; ++i)
					{
						std::memcpy(dst+4*i,&pixel,4);
					}
				}
				dst+=4*n;
			}
			return true;
		}
		for(int x=0; x<xEnd && inLine<inLineLimit;)
		{
			const unsigned short col16=cpputil::GetWord(src);
			int n=ZHsrc[ZHswitch];
			const bool whole=(n<=xEnd-x);
			if(true!=whole)
			{
				n=xEnd-x;
			}
			if(true==transparentRow && 0!=(col16&0x8000))
			{
				dst+=4*n;
			}
			else
			{
				unsigned char pixel[4];
				pixel[0]=((col16>>2)&0xf8)|((col16>>7)&7);
				pixel[1]=((col16>>7)&0xf8)|((col16>>12)&7);
				pixel[2]=((col16<<3)&0xf8)|((col16>>2)&7);
				pixel[3]=255;
				for(int i=0; i<n; ++i)
				{
					std::memcpy(dst,pixel,4);
					dst+=4;
				}
			}
			x+=n;
			if(true!=whole)
			{
				break;
			}
			ZHswitch=1-ZHswitch;
			inLine+=2;
			src+=2;
		}
		return true;
	};
	if (true != transparent)
	{
		for (int y = 0; y < layer.sizeOnMonitor.y() && y + layer.originOnMonitor.y() <= bottomY; y += yStep)
		{
			auto Y = y + layer.originOnMonitor.y();
			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
				if(true!=drawRow(dst,false))
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine + RenderOffsetX; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

				unsigned short col16 = cpputil::GetWord(VRAM + VRAMAddr);
'''),
        ('tsugaru/src/towns/render/render.cpp',
         '''			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
			for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine + RenderOffsetX; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

				unsigned short col16 = cpputil::GetWord(VRAM + VRAMAddr);
''',
         '''			unsigned char* dstLine = rgba.data() + 4 * (Y * this->wid + X);
			auto dst = dstLine;

			unsigned int inLineVRAMOffset = layer.VRAMHSkipBytes;
			int ZHswitch = 0;
			auto ZH = ZHsrc[ZHswitch];
				if(true!=drawRow(dst,true))
				for (int x = 0; x < layer.sizeOnMonitor.x() && x + layer.originOnMonitor.x() < this->wid && inLineVRAMOffset < layer.bytesPerLine + RenderOffsetX; x++)
			{
				unsigned int VRAMAddr = lineVRAMOffset + ((inLineVRAMOffset + VRAMOffsetHorizontal) & VRAMHScrollMask);
				VRAMAddr = VRAMBase + ((VRAMAddr + VRAMOffsetVertical) & VRAMVScrollMask);
				OFFSETTRANS::Trans(VRAMAddr);

				unsigned short col16 = cpputil::GetWord(VRAM + VRAMAddr);
'''),
    ],
    'tsugaru/src/towns/render/render.h': [
        ('tsugaru/src/towns/render/render.h',
         '''	*/
	void BuildImage(const unsigned char VRAM[],const TownsCRTC::AnalogPalette &palette,const TownsCRTC::ChaseHQPalette &chaseHQ);

	/*!
	*/
	void FlipUpsideDown(void);
''',
         '''	*/
	void BuildImage(const unsigned char VRAM[],const TownsCRTC::AnalogPalette &palette,const TownsCRTC::ChaseHQPalette &chaseHQ);

	/*! KairoTowns: how many bytes from the start of VRAM BuildImage can read with the screen as
	    Prepare saw it, at most vramSize. A copy of that much of VRAM draws the same picture as all
	    of it (KairoTowns#34).
	*/
	size_t KairoVRAMBytesRead(size_t vramSize) const;

	/*!
	*/
	void FlipUpsideDown(void);
'''),
        ('tsugaru/src/towns/render/render.h',
         '''	*/
	ImageCopy MoveImage(void);

	class VRAM0Trans // 80000000H to 80080000H
	{
	public:
''',
         '''	*/
	ImageCopy MoveImage(void);

	/*! KairoTowns: exchanges the image with img, whose bitmap this TownsRender draws the next image into.
	    Unlike MoveImage, the next BuildImage of the same size reuses that bitmap instead of allocating one.
	*/
	void SwapImage(ImageCopy &img);

	class VRAM0Trans // 80000000H to 80080000H
	{
	public:
'''),
    ],
    'tsugaru/src/towns/rtc/rtc.cpp': [
        ('tsugaru/src/towns/rtc/rtc.cpp',
         '''		}
		if(state.state!=STATE_NONE)
		{
			auto t=time(nullptr);
			auto tm=localtime(&t);
			switch(state.registerLatch)
			{
			case REG_1SEC://0
''',
         '''		}
		if(state.state!=STATE_NONE)
		{
			time_t t=time(nullptr);
			if(0!=fixedTime) // KairoTowns
			{
				t=(time_t)(fixedTime+(long long int)(townsPtr->state.townsTime/1000000000));
			}
			auto tm=(0!=fixedTime ? gmtime(&t) : localtime(&t));
			switch(state.registerLatch)
			{
			case REG_1SEC://0
'''),
    ],
    'tsugaru/src/towns/rtc/rtc.h': [
        ('tsugaru/src/towns/rtc/rtc.h',
         '''public:
	virtual const char *DeviceName(void) const{return "RTC";}

	enum
	{
		REG_1SEC=0,
''',
         '''public:
	virtual const char *DeviceName(void) const{return "RTC";}

	/*! KairoTowns: when not 0, the clock reads this time plus the guest time run, instead of the
	    host's clock, so runs repeat exactly; for tests that compare two runs.
	*/
	long long int fixedTime=0;

	enum
	{
		REG_1SEC=0,
'''),
    ],
    'tsugaru/src/towns/scsi/scsi.cpp': [
        ('tsugaru/src/towns/scsi/scsi.cpp',
         '''
#include "towns.h"



////////////////////////////////////////////////////////////
''',
         '''
#include "towns.h"

// KairoTowns: where a disk a save state names is now (shared kairo/state_paths.h).
extern "C" const char *kairo_state_path(const char *recorded);



////////////////////////////////////////////////////////////
'''),
        ('tsugaru/src/towns/scsi/scsi.cpp',
         '''	for(auto &dev : state.dev)
	{
		dev.devType=ReadUint32(data);
		std::string fName=ReadString(data);
		std::string relPath;
		if(1<=version)
		{
''',
         '''	for(auto &dev : state.dev)
	{
		dev.devType=ReadUint32(data);
		std::string fName=std::string(kairo_state_path(ReadString(data).c_str())); // KairoTowns
		std::string relPath;
		if(1<=version)
		{
'''),
    ],
    'tsugaru/src/towns/towns.cpp': [
        ('tsugaru/src/towns/towns.cpp',
         '''
void FMTownsCommon::NotifyDiskRead(void)
{
	keyboard.BootSequenceStarted();
	gameport.BootSequenceStarted();
	state.noWait=var.noWaitStandby;
''',
         '''
void FMTownsCommon::NotifyDiskRead(void)
{
	++kairoDiskReads; // KairoTowns
	keyboard.BootSequenceStarted();
	gameport.BootSequenceStarted();
	state.noWait=var.noWaitStandby;
'''),
    ],
    'tsugaru/src/towns/towns.h': [
        ('tsugaru/src/towns/towns.h',
         '''	*/
	void NotifyDiskRead(void);


	/*! Process sound.
	*/
''',
         '''	*/
	void NotifyDiskRead(void);

	/*! KairoTowns: counts NotifyDiskRead calls, so the machine knows a boot has read its first disk.
	*/
	uint64_t kairoDiskReads=0;


	/*! Process sound.
	*/
'''),
    ],
}
