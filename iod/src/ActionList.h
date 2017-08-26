#ifndef __ACTIONLIST_H__
#define __ACTIONLIST_H__

#include <list>
#include <boost/thread/recursive_mutex.hpp>
class Action;

class ActionList {
public:
	void push_back(Action *);
	void push_front(Action *);
	Action *back();
	Action *front();
	void pop_front();
	size_t size();
	bool empty();
	std::list<Action*>::iterator begin();
	std::list<Action*>::iterator end();
	std::list<Action*>::iterator erase(std::list<Action*>::iterator &i);
	void remove(Action *a);
private:
	boost::recursive_mutex mutex;
	std::list<Action*>actions;
};

#endif
