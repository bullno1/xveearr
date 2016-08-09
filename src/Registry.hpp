#ifndef XVEEARR_REGISTRY_HPP
#define XVEEARR_REGISTRY_HPP

#define XVR_DECLARE_REGISTRY(T) \
	namespace xveearr { \
		template<> Registry< T >::Entry* Registry< T >::sEntry = NULL; \
	}

namespace xveearr
{

template<typename BaseT>
class Registry
{
	friend class Entry;
public:
	class Entry;

	class Iterator
	{
	public:
		Iterator(Entry* entry);

		Iterator& operator++();
		BaseT* operator->();
		BaseT& operator*();

		bool operator==(const Iterator& other) const;
		bool operator!=(const Iterator& other) const;

	private:
		Entry* mEntry;
	};

	class Range
	{
	public:
		Range(Entry* entry);

		Iterator begin();
		Iterator end();

	private:
		Entry* mEntry;
	};

	class Entry
	{
		friend class Iterator;
	public:
		template<typename DerivedT>
		Entry(DerivedT* instance);

	private:
		Entry* mNext;
		BaseT* mObj;
	};


	static Range all();

private:
	static Entry* sEntry;
};

}

#include "Registry.inl"

#endif
