#pragma once
#include "types/types.hpp"
#include "system/system.hpp"
#include "system/log.hpp"
#include "utils/math.hpp"
#define _inline_ 

#ifdef _WIN32
#include <Windows.h>
#undef min
#undef max
#undef far
#undef near
#else
#include <sys/mman.h>
#endif

namespace emu {

	template<typename AddressType>
	struct MemoryRange {

		String name, altName;
		Buffer initMemory;

		AddressType start, size;
		bool write;

		explicit MemoryRange(AddressType start, AddressType size, bool write, String name, String altName, Buffer initMemory):
			start(start), size(size), write(write), name(name), altName(altName), initMemory(initMemory) {}

	};
	template<typename AddressType, typename Mapping>
	class Memory;

	template<typename AddressType, typename Mapping>
	struct MemoryPointer {

		Memory<AddressType, Mapping> *memory;
		AddressType v;

		constexpr MemoryPointer(Memory<AddressType, Mapping> *memory, AddressType v): memory(memory), v(v) {}

		constexpr _inline_ usz addr() const { return Mapping::map(memory, v); }

		template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> || std::is_pod_v<T>>>
		constexpr _inline_ operator T() const {
			return Mapping::read<T>(memory, v);
		}

		template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> || std::is_pod_v<T>>>
		constexpr _inline_ void operator=(const T &t) const {
			Mapping::write(memory, v, t);
		}

	};

	//Some things don't reside "in memory" for the emulated program
	//But they are still present elsewhere; this data can be accessed by the memory mapper to simulate banked memory
	struct MemoryBanks {

		usz banks, bankSize;
		Buffer allocated;

		MemoryBanks(usz banks, usz bankSize): banks(banks), bankSize(bankSize), allocated(banks * bankSize) { }

		MemoryBanks(usz banks, usz bankSize, Buffer def, usz offset, usz end): 
			MemoryBanks(banks, bankSize) {
			if (offset <= def.size() && end <= def.size())
				memcpy(allocated.data(), def.data() + offset, 
					   oic::Math::min(end - offset, allocated.size()));
		}

		//Get the address of the bank
		//Out of bounds not checked
		_inline_ usz bank(usz i) const {
			return usz(allocated.data()) + i * bankSize;
		}

	};

	template<typename AddressType, typename Mapping>
	class Memory {

	public:

		using Range = MemoryRange<AddressType>;

	private:

		static void allocate();
		static void allocate(Range &r);
		static void free();

		static void initMemory(Range &r, void *ou) {

			memset(ou, 0, r.size);

			if (r.initMemory.size() <= r.size) {
				if (r.initMemory.size() != 0) {
					memcpy(ou, r.initMemory.data(), r.initMemory.size());
					r.initMemory.clear();
				}
			} else
				oic::System::log()->fatal("Couldn't initialize memory");
		}

	public:

		Memory(const List<Range> &ranges_, const List<MemoryBanks> &banks): ranges(ranges_), banks(banks) {

			static_assert(sizeof(AddressType) <= sizeof(usz), "32-bit architectures can't support 64 architectures");

			allocate();

			for (Range &range : ranges)
				if(range.size)
					allocate(range);
		}

		~Memory() {
			free();
		}

		//Gets the variable from the address (read)
		template<typename T>
		_inline_ const T get(AddressType ptr) {
			return MemoryPointer<AddressType, Mapping>((Memory*)this, ptr).operator T();
		}

		//Sets the variable at the address (write)
		template<typename T>
		_inline_ void set(AddressType ptr, const T &t) {
			MemoryPointer<AddressType, Mapping>(this, ptr).operator=(t);
		}

		Memory(const Memory&) = delete;
		Memory(Memory&&) = delete;
		//Memory &operator=(const Memory&) = delete;
		//Memory &operator=(Memory&&) = delete;

		_inline_ const List<Range> &getRanges() const { return ranges; }
		_inline_ const List<MemoryBanks> &getBanks() const { return banks; }

		_inline_ usz getBanked(usz bankRegister, usz bankId) const { 
			return banks[bankRegister].bank(bankId); 
		}

		_inline_ usz getBankedMemory(usz bankRegister, usz bankId, usz offset) const { 
			return getBanked(bankRegister, bankId) + offset; 
		}

	private:

		List<Range> ranges;
		List<MemoryBanks> banks;

	};

	template<usz start, usz size>
	struct DefaultMappingFunc {

		static constexpr usz virtualMemory[2] = { start, size };

		static usz __forceinline map(u16 x) { return mapping | x; } 
	};

	template<typename MappingFunc>
	using Memory16 = Memory<u16, MappingFunc>;

	template<typename MappingFunc>
	using Memory32 = Memory<u32, MappingFunc>;

	template<typename MappingFunc>
	using Memory64 = Memory<u64, MappingFunc>;

	#ifdef _WIN32

		template<typename AddressType, typename Mapping>
		void Memory<AddressType, Mapping>::allocate() {

			if (!VirtualAlloc(LPVOID(Mapping::virtualMemory[0]), Mapping::virtualMemory[1], MEM_RESERVE, PAGE_READWRITE))
				oic::System::log()->fatal("Couldn't reserve memory");
		}

		template<typename AddressType, typename Mapping>
		void Memory<AddressType, Mapping>::allocate(Range &r) {

			usz map = Mapping::virtualMemory[0] | r.start;

			if (!VirtualAlloc(LPVOID(map), usz(r.size), MEM_COMMIT, PAGE_READWRITE))
				oic::System::log()->fatal("Couldn't allocate memory");

			initMemory(r, (void*)map);

			DWORD oldProtect;

			if (!r.write && !VirtualProtect((void*)map, usz(r.size), PAGE_READONLY, &oldProtect))
				oic::System::log()->fatal("Couldn't protect memory");
		}

		template<typename AddressType, typename Mapping>
		void Memory<AddressType, Mapping>::free() {
			VirtualFree(LPVOID(Mapping::virtualMemory[0]), 0, MEM_RELEASE);
		}

	#else

		template<typename AddressType, typename Mapping>
		void Memory<AddressType, Mapping>::allocate() {

			if(!mmap((void*)Mapping::virtualMemory[0], Mapping::virtualMemory[1], PROT_NONE, MAP_PRIVATE | MAP_FIXED, 0))
				oic::System::log()->fatal("Couldn't reserve memory");
		}

		template<typename AddressType, typename Mapping>
		void Memory<AddressType, Mapping>::allocate(Range &r) {

			usz map = Mapping::virtualMemory[0] | r.start;
			
			if(!mmap((void*)map, usz(r.size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0))
				oic::System::log()->fatal("Couldn't allocate memory");

			initMemory(r, (void*)map);

			if (!r.write && mprotect((void*)map, usz(r.size), PROT_READ))
				oic::System::log()->fatal("Couldn't protect memory");
		}

		template<typename AddressType, typename Mapping>
		void Memory<AddressType, Mapping>::allocate() {
			munmap((void*)Mapping::virtualMemory[0], Mapping::virtualMemory[1]);
		}

	#endif

}