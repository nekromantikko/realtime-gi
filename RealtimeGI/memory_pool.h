#pragma once
#include "typedef.h"

template<class T>
class MemoryPool
{
private:
	T* objs;
	s32* handles;
	u32 size;
	u32 count;
public:
	MemoryPool() {
		Init(0);
	}
	MemoryPool(u32 s) {
		Init(s);
	}
	~MemoryPool() {
		free(objs);
		free(handles);
	}
	void Init(u32 s) {
		size = s;
		count = 0;

		objs = (T*)calloc(size, sizeof(T));
		handles = (s32*)calloc(size, sizeof(s32));

		for (u32 i = 0; i < size; i++)
		{
			handles[i] = i;
		}
	}
	s32 Add(const T& obj) {
		if (count >= size) {
			return -1;
		}

		s32 handle = handles[count++];
		objs[handle] = obj;
		return handle;
	}
	T& operator[](s32 handle) {
		return objs[handle];
	}
	bool Remove(const s32 handle) {
		// TODO: Smart find algorithm if this gets slow
		for (u32 i = 0; i < count; i++) {
			if (handles[i] == handle) {
				handles[i] = handles[--count];
				handles[count] = handle;
				return true;
			}
		}
		return false;
	}
	u32 Count() const {
		return count;
	}
};