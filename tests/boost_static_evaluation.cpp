#include <boost/container/static_vector.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/static_string.hpp>
#include <boost/version.hpp>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
// Count C++ general allocation during successful operations. Exception runtime
// allocation is deliberately outside this scope; malloc is not intercepted.
static unsigned allocations;
void* operator new(std::size_t n){++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
struct Node:boost::intrusive::list_base_hook<> {int value;explicit Node(int v):value(v){}};
using List=boost::intrusive::list<Node>;
constexpr boost::static_strings::static_string<8> fixed("nexrt");
static_assert(fixed.size()==5);
int main(){
 unsigned before=allocations;
 boost::container::static_vector<int,2> v;
 v.push_back(4);v.push_back(5);assert(v.size()==2&&v[1]==5);
 Node a(1),b(2);List list;list.push_back(a);list.push_back(b);
 assert(&list.front()==&a&&a.is_linked());list.erase(list.iterator_to(a));
 assert(!a.is_linked()&&list.front().value==2);list.clear();assert(!b.is_linked());
 boost::static_strings::static_string<8> text("nexrt");text.append("123");assert(text.size()==8);
 const unsigned success_allocations=allocations-before;assert(success_allocations==0);
 bool vector_overflow=false,string_overflow=false;
 try{v.push_back(6);}catch(const boost::container::bad_alloc&){vector_overflow=true;}
 try{text.push_back('4');}catch(const std::length_error&){string_overflow=true;}
 assert(vector_overflow&&string_overflow&&v.size()==2&&text.size()==8);
 std::printf("BOOST_VERSION=%d successful_operation_new_calls=%u overflow_checks=2 passed\n",BOOST_VERSION,success_allocations);
}
