#include "Registry.hpp"

namespace xveearr
{

template<typename BaseT>
inline typename Registry<BaseT>::Range Registry<BaseT>::Registry::all()
{
	return Range(sEntry);
}

template<typename BaseT>
template<typename DerivedT>
inline Registry<BaseT>::Entry::Entry(DerivedT* instance)
{
	mNext = Registry<BaseT>::sEntry;
	Registry<BaseT>::sEntry = this;
	mObj = static_cast<BaseT*>(static_cast<DerivedT*>(instance));
}

template<typename BaseT>
inline Registry<BaseT>::Iterator::Iterator(typename Registry<BaseT>::Entry* entry)
	:mEntry(entry)
{}

template<typename BaseT>
inline typename Registry<BaseT>::Iterator& Registry<BaseT>::Iterator::operator++()
{
	mEntry = mEntry->mNext;
	return *this;
}

template<typename BaseT>
inline BaseT* Registry<BaseT>::Iterator::operator->()
{
	return mEntry->mObj;
}

template<typename BaseT>
inline BaseT& Registry<BaseT>::Iterator::operator*()
{
	return *(mEntry->mObj);
}

template<typename BaseT>
inline bool Registry<BaseT>::Iterator::operator==(
	const typename Registry<BaseT>::Iterator& other) const
{
	return mEntry == other.mEntry;
}

template<typename BaseT>
inline bool Registry<BaseT>::Iterator::operator!=(
	const typename Registry<BaseT>::Iterator& other) const
{
	return mEntry != other.mEntry;
}

template<typename BaseT>
inline Registry<BaseT>::Range::Range(Entry* entry)
	:mEntry(entry)
{}

template<typename BaseT>
inline typename Registry<BaseT>::Iterator Registry<BaseT>::Range::begin()
{
	return Iterator(mEntry);
}

template<typename BaseT>
inline typename Registry<BaseT>::Iterator Registry<BaseT>::Range::end()
{
	return Iterator(0);
}

}
