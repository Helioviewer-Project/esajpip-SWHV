#ifndef _BASE_H_
#define _BASE_H_


#include <map>
#include <vector>
#include <sstream>


/**
 * Contains a set of useful static methods used by the application.
 */
struct base
{

  /**
   * Converts a value to a string.
   * @param val Value to convert.
   */
  template<typename TYPE> static std::string to_string(TYPE val)
  {
    std::ostringstream oss;
    oss << val;
    return oss.str();
  }

  /**
   * Copies a vector.
   */
  template<typename T> static void copy(std::vector<T>& dest, const std::vector<T>& src)
  {
    dest.clear();
    for (typename std::vector<T>::const_iterator i = src.begin(); i != src.end(); i++)
      dest.push_back(*i);
  }

  /**
   * Copies a vector of vectors.
   */
  template<typename T> static void copy(std::vector< std::vector<T> >& dest, const std::vector< std::vector<T> >& src)
  {
    int n = 0;
    dest.clear();
    dest.resize(src.size());
    for (typename std::vector< std::vector<T> >::const_iterator i = src.begin(); i != src.end(); i++)
      base::copy(dest[n++], *i);
  }

  /**
   * Copies a multimap.
   */
  template<typename T1, typename T2> static void copy(std::multimap<T1, T2>& dest, const std::multimap<T1, T2>& src)
  {
	dest.clear();
    for (typename std::multimap<T1, T2>::const_iterator i = src.begin(); i != src.end(); i++)
      dest.insert(*i);
  }

};


#endif /* _BASE_H_ */
