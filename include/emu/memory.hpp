#pragma once
#include "types/types.hpp"
#include "system/system.hpp"
#include "system/allocator.hpp"
#include "system/log.hpp"
#include "utils/math.hpp"

#ifdef NDEBUG
	#define _inline_ __forceinline
#else
	#define _inline_
#endif

namespace emu {

	template<typename AddressType>
	struct MemoryRange {

		String name, altName;
		Buffer initMemory;

		AddressType start, size;
		bool write, allocate;

		//If !write, the memory range will be initialized and protected against writing
		//If !allocate, the memory range won't be allocated but only reserved
		_inline_ explicit MemoryRange(
			AddressType start, AddressType size, bool write, String name, String altName, 
			Buffer initMemory, bool allocate = true
		):
			start(start), size(size), write(write), name(name), altName(altName), 
			initMemory(initMemory), allocate(allocate) {}

		_inline_ usz end() const {
			return usz(start) + size;
		}

	};

	using ProgramMemoryRange = MemoryRange<usz>;

	template<typename AddressType, typename Mapping>
	class Memory;

	template<typename AddressType, typename Mapping, typename T = void>
	struct MemoryPointer {

		Memory<AddressType, Mapping> *memory;
		AddressType v;

		constexpr MemoryPointer(Memory<AddressType, Mapping> *memory, AddressType v): memory(memory), v(v) {
		
			static_assert(
				std::is_arithmetic_v<T> || std::is_pod_v<T>, 
				"Typed MemoryPointer requires an arithmetic or pod type"
			);
		}

		constexpr _inline_ operator T() const {
			return Mapping::read<T>(memory, v);
		}

		constexpr _inline_ const MemoryPointer &operator=(const T &t) const {
			Mapping::write(memory, v, t);
			return *this;
		}

		constexpr _inline_ const MemoryPointer &operator+=(T t) const {
			t += Mapping::read<T>(memory, v);
			Mapping::write(memory, v, t);
			return *this;
		}

		constexpr _inline_ const MemoryPointer &operator|=(T t) const {
			t |= Mapping::read<T>(memory, v);
			Mapping::write(memory, v, t);
			return *this;
		}

		constexpr _inline const MemoryPointer &operator++() const {
			return operator+=(1);
		}

	};

	template<typename AddressType, typename Mapping>
	struct MemoryPointer<AddressType, Mapping, void> {

		Memory<AddressType, Mapping> *memory;
		AddressType v;

		constexpr MemoryPointer(Memory<AddressType, Mapping> *memory, AddressType v): memory(memory), v(v) {}

		constexpr _inline_ usz addr() const { return Mapping::map(memory, v); }

		template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> || std::is_pod_v<T>>>
		constexpr _inline_ operator T() const {
			return Mapping::read<T>(memory, v);
		}

		template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> || std::is_pod_v<T>>>
		constexpr _inline_ T operator=(const T &t) const {
			Mapping::write(memory, v, t);
			return t;
		}

	};

	template<typename AddressType, typename Mapping>
	class Memory {

	public:

		using Range = MemoryRange<AddressType>;
		using Pointer = MemoryPointer<AddressType, Mapping>;

		template<typename T>
		using TypedPointer = MemoryPointer<AddressType, Mapping, T>;

	private:

		static _inline_ void reserve(usz start, usz end);
		static _inline_ void allocate(usz start, usz size, bool writable, const Buffer &initMemory);
		static _inline_ void free(usz start, usz end);

	public:

		//The program's regular memory and the simulated memory inside.
		//The simulated ranges are always stored inside memory[0]
		//The size of the reserved memory is assumed to be from memory[0].start til memory.last().end()
		Memory(const List<ProgramMemoryRange> &memory_, const List<Range> &ranges_):
			ranges(ranges_), memory(memory_)
		{

			static_assert(sizeof(AddressType) <= sizeof(usz), "32-bit architectures can't support 64 architectures");

			//Reserve all of the memory
			reserve(memory[0].start, memory[memory.size() - 1].end());

			//Allocate the memory to those ranges
			for(ProgramMemoryRange &range : memory)
				if (range.size && range.allocate)
					allocate(range.start, range.size, range.write, range.initMemory);

			//Allocate the virtual ranges
			for (Range &range : ranges)
				if(range.size && range.allocate)
					allocate(memory[0].size + range.start, range.size, range.write, range.initMemory);
		}

		~Memory() {
			free(memory[0].start, memory[memory.size() - 1].end());
		}

		//Gets the variable from the address (read)
		template<typename T>
		_inline_ const T get(AddressType ptr) {
			return Pointer((Memory*)this, ptr);
		}

		//Gets the variable from the address (read)
		template<typename T = u8>
		_inline_ const TypedPointer<T> operator[](AddressType ptr) {
			return TypedPointer<T>((Memory*)this, ptr);
		}

		//Sets the variable at the address (write)
		template<typename T>
		_inline_ void set(AddressType ptr, const T &t) {
			Pointer(this, ptr) = t;
		}

		//Increment variable at the address
		template<typename T, T incr = 1>
		_inline_ T increment(AddressType ptr) {
			Pointer mem(this, ptr);
			return mem = T(mem.operator T() + incr);
		}

		Memory(const Memory&) = delete;
		Memory(Memory&&) = delete;
		Memory &operator=(const Memory&) = delete;
		Memory &operator=(Memory&&) = delete;

		_inline_ const List<Range> &getRanges() const { return ranges; }
		_inline_ const List<ProgramMemoryRange> &getMemory() { return memory; }

		template<typename T>
		_inline_ T &getMemory(usz v) {
			return *(T*)v;
		}

		//Get the T& of the currently accessible memory
		//This should only be used with STATIC addresses; like I/O registers.
		template<typename T>
		_inline_ T &getRef(AddressType t) {
			return *(T*)(Mapping::mapping | t);
		}

	private:

		List<Range> ranges;
		List<ProgramMemoryRange> memory;

	};

	template<typename MappingFunc>
	using Memory16 = Memory<u16, MappingFunc>;

	template<typename MappingFunc>
	using Memory32 = Memory<u32, MappingFunc>;

	template<typename MappingFunc>
	using Memory64 = Memory<u64, MappingFunc>;

	template<typename AddressType, typename Mapping>
	void Memory<AddressType, Mapping>::reserve(usz start, usz size) {

		if(!oic::System::allocator()->allocRange(start, size, (u8*)nullptr, oic::Allocator::RESERVE))
			oic::System::log()->fatal("Couldn't reserve memory");
	}

	template<typename AddressType, typename Mapping>
	void Memory<AddressType, Mapping>::allocate(usz start, usz size, bool write, const Buffer &mem) {

		if (mem.size() > size)
			oic::System::log()->fatal("Couldn't initialize memory");

		auto commitFlags = write ? oic::Allocator::COMMIT : oic::Allocator::READ_ONLY | oic::Allocator::COMMIT;

		if(!oic::System::allocator()->allocRange(
			start, size, mem.data(), oic::Allocator::RangeHint(commitFlags)
		))
			oic::System::log()->fatal("Couldn't allocate memory");

	}

	template<typename AddressType, typename Mapping>
	void Memory<AddressType, Mapping>::free(usz start, usz size) {
		oic::System::allocator()->freeRange((u8*)start, size);
	}

}