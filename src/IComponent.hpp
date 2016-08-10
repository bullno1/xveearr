#ifndef XVEEEARR_COMPONENT_HPP
#define XVEEEARR_COMPONENT_HPP

namespace xveearr
{

class IComponentBase
{
public:
	virtual void shutdown() = 0;
	virtual const char* getName() const = 0;
};

template<typename T>
class IComponent: public IComponentBase
{
public:
	virtual bool init(const T& initParam) = 0;
};

template<>
class IComponent<void>: public IComponentBase
{
public:
	virtual bool init() = 0;
};

}

#endif
