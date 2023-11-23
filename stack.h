#ifndef STACK_H
#define STACK_H

#include "namedobject.h"
#include "options.h"
#include "utils.h"
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <vector>

template<typename T>
class Stack
{
public:
    // Expose this so we can use it as InterfaceList for example.
    typedef std::map<std::string, T> MapType;

private:
    using MapIter = typename MapType::const_iterator;
    using StackType = std::deque<MapType>;
    using StackRIter = typename StackType::const_reverse_iterator;

public:
    Stack() { NewLevel(); }
    void NewLevel() { stack.push_back(MapType()); }

    size_t MaxLevel() const { return stack.size() - 1; }

    const MapType& GetLevel() { return stack.back(); }

    void DropLevel() { stack.pop_back(); }

    /* Returns false on failure */
    bool Add(std::string name, const T& v)
    {
	if (caseInsensitive)
	{
	    strlower(name);
	}
	MapIter it = stack.back().find(name);
	if (it == stack.back().end())
	{
	    if (verbosity > 1)
	    {
		std::cerr << "Adding value: " << name << std::endl;
	    }
	    stack.back()[name] = v;
	    return true;
	}
	return false;
    }
    // Alternative version, used with NamedObject
    bool Add(const T& v) { return Add(v->Name(), v); }

    T Find(std::string name, size_t& level) const
    {
	int lvl = MaxLevel();
	if (verbosity > 1)
	{
	    std::cerr << "Finding value: " << name << std::endl;
	}
	if (caseInsensitive)
	{
	    strlower(name);
	}
	for (StackRIter s = stack.rbegin(); s != stack.rend(); s++, lvl--)
	{
	    MapIter it = s->find(name);
	    if (it != s->end())
	    {
		level = lvl;
		if (verbosity > 1)
		{
		    std::cerr << "Found at lvl " << lvl << std::endl;
		}
		return it->second;
	    }
	}
	if (verbosity > 1)
	{
	    std::cerr << "Not found" << name << std::endl;
#if !NDEBUG
	    dump();
#endif
	}
	return 0;
    }

    T Find(const std::string& name)
    {
	size_t dummy;
	return Find(name, dummy);
    }

    T FindTopLevel(std::string name)
    {
	if (caseInsensitive)
	{
	    strlower(name);
	}
	MapIter it = stack.back().find(name);
	if (it != stack.back().end())
	{
	    return it->second;
	}
	return 0;
    }

    void dump() const;

private:
    StackType stack;
};

template<typename T>
class StackWrapper
{
public:
    StackWrapper(Stack<T>& v) : stack(v) { stack.NewLevel(); }
    ~StackWrapper() { stack.DropLevel(); }

private:
    Stack<T>& stack;
};

#if !NDEBUG
template<typename T>
void Stack<T>::dump() const
{
    int n = 0;
    for (auto s : stack)
    {
	std::cerr << "Level " << n << std::endl;
	n++;
	for (auto v : s)
	{
	    std::cerr << v.first << ": ";
	    v.second->dump();
	    std::cerr << std::endl;
	}
    }
}
#endif

class InterfaceList
{
public:
    InterfaceList(){};
    bool                                      Add(const NamedObject* obj);
    const Stack<const NamedObject*>::MapType& List() const { return list; }

private:
    Stack<const NamedObject*>::MapType list;
};

#endif
