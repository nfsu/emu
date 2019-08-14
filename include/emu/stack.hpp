#pragma once
#include "types/types.hpp"

namespace emu {

	//The stack class; taking a type T (can even be floats)
	//AddressSpace is the type for the address space
	//This is an interface for how stacks work in low-level architectures and doesn't provide memory management
	//If ascending = true, the stack pointer will increase if you push (memory address grows), otherwise it will decrease
	//If isEmpty = true, the stack pointer will point to the next address where something should be pushed. Otherwise points to last object pushed
	template<
		typename T, typename AddressSpace, typename Memory,
		bool isAscending = false, bool isEmpty = false
	>
	class TStack {

	public:

		static constexpr AddressSpace increment = 
			!isAscending ? 
			AddressSpace (-Signed_v<AddressSpace>(sizeof(T))) : 
			AddressSpace(sizeof(T));

		_inline_ static void push(Memory &m, AddressSpace &sp, const T &a) {

			if constexpr (!isEmpty) {
				sp -= sizeof(T) - 1;
				m.set(sp, a);
				--sp;
			} else {
				sp += increment;
				m.set(sp, a);
			}

		}

		template<typename ...args>
		_inline_ static void push(Memory &m, AddressSpace &sp, const T &a, const args &...arg) {
			push(m, sp, a);
			push(m, sp, arg...);
		}

		_inline_ static void pop(Memory &m, AddressSpace &sp, T &a) {

			if constexpr (!isEmpty) {
				++sp;
				a = m.get<T>(sp);
				sp += sizeof(T) - 1;
			} else {
				sp -= increment;
				a = m.get<T>(sp);
			}

		}

		template<typename ...args>
		_inline_ static void pop(Memory &m, AddressSpace &sp, T &a, args &...arg) {
			pop(m, sp, a);
			pop(m, sp, arg...);
		}

	};

	//Different stack types

	template<typename Memory, typename AddressType, bool isAscending = false, bool isEmpty = false>
	using Stack = TStack<AddressType, AddressType, Memory, isAscending, isEmpty>;

}