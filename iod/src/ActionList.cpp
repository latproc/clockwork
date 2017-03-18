#include "ActionList.h"

void ActionList::push_back(Action *a) {
	boost::recursive_mutex::scoped_lock lock(mutex);
	actions.push_back(a);
}

void ActionList::push_front(Action *a){
	boost::recursive_mutex::scoped_lock lock(mutex);
	actions.push_front(a);
}

Action *ActionList::back(){
	boost::recursive_mutex::scoped_lock lock(mutex);
	return actions.back();
}

Action *ActionList::front(){
	boost::recursive_mutex::scoped_lock lock(mutex);
	return actions.front();
}
void ActionList::pop_front(){
	boost::recursive_mutex::scoped_lock lock(mutex);
	actions.pop_front();
}

size_t ActionList::size(){
	boost::recursive_mutex::scoped_lock lock(mutex);
	return actions.size();
}

bool ActionList::empty(){
	boost::recursive_mutex::scoped_lock lock(mutex);
	return actions.empty();
}

std::list<Action*>::iterator ActionList::begin(){
	boost::recursive_mutex::scoped_lock lock(mutex);
	return actions.begin();
}

std::list<Action*>::iterator ActionList::end(){
	boost::recursive_mutex::scoped_lock lock(mutex);
	return actions.end();
}

std::list<Action*>::iterator ActionList::erase(std::list<Action*>::iterator &i){
	boost::recursive_mutex::scoped_lock lock(mutex);
	return actions.erase (i);
}

void ActionList::remove(Action *a){
	boost::recursive_mutex::scoped_lock lock(mutex);
	actions.remove(a);
}
