#ifndef _DATA_SERIALIZE_H_
#define _DATA_SERIALIZE_H_


#include <stdint.h>
#include <string>
#include <vector>
#include <map>
#include "file.h"


namespace data
{
  using namespace std;

  /**
   * This struct identifies a basic input operator to be applied
   * to a <code>File</code> object.
   *
   * @see BaseStream
   * @see File
   */
  struct InputOperator
  {
	/**
	 * Returns the required file access for this operator.
	 */
    inline static const char *FileAccess()
    {
      return "rb";
    }

    /**
     * Performs an input (read) serialization of bytes for a file.
     * @param file File to use for the operation.
     * @param ptr Pointer to the buffer where to store the bytes.
     * @param num_bytes Number of bytes to read from the file.
     * @return <code>true</code> if successful.
     */
    inline static bool SerializeBytes(File& file, void *ptr, int num_bytes)
    {
      return file.Read(ptr, num_bytes);
    }
  };

  /**
   * This struct identifies a basic output operator to be applied
   * to a <code>File</code> object.
   *
   * @see BaseStream
   * @see File
   */
  struct OutputOperator
  {
	/**
	 * Returns the required file access for this operator.
	 */
    inline static const char *FileAccess()
    {
      return "wb";
    }

    /**
     * Performs an output (write) serialization of bytes for a file.
     * @param file File to use for the operation.
     * @param ptr Pointer to the buffer where to read the bytes.
     * @param num_bytes Number of bytes to write to the file.
     * @return <code>true</code> if successful.
     */
    inline static bool SerializeBytes(File& file, void *ptr, int num_bytes)
    {
      return file.Write(ptr, num_bytes);
    }
  };

  /**
   * This template is used as the base for the input/output stream classes.
   * Contains the basic functionality for the serialization with files,
   * composed by a file object and an internal status. This status is updated
   * with each operation and, in a sequence of serialization, this is
   * stopped just after this status is set to <code>false</code>.
   *
   * In order to have a type serializable, it must comply with one of these
   * requirements: i) to have implemented a "serializer" with the class
   * <code>Serializer</code>, or ii) to have defined a member method called
   * <code>SerializeWith</code>.
   *
   * The first option is useful for the basic types (int, float, etc.) and
   * for those classes already defined and that can not be modified. The
   * second option is more elegant for those classes that can be modified
   * specifically for serialization.
   *
   * The <code>SerializeWith</code> method must be defined as follows:
   *
   * <code>
   * template<typename T> T& SerializeWith(T& stream)
   * {
   *   return (stream & member1 & member2 & ...);
   * }
   * </code>
   *
   * @see Serializer
   * @see InputStream
   * @see OutputStream
   */
  template<typename StreamClass, typename StreamOperator> class BaseStream
  {
  protected:
    File file_;		///< File used for the serialization
    bool result_;	///< Internal current status of the serialization

  public:
    /**
     * Initializes the status to <code>false</code>.
     */
    BaseStream()
    {
      result_ = false;
    }

    /**
     * Opens a file for serialization.
     * @param file_name Path name of the file to open.
     * @return <code>*this</code>.
     */
    StreamClass& Open(const char *file_name)
    {
      result_ = file_.Open(file_name, StreamOperator::FileAccess());
      return (StreamClass&) (*this);
    }

    /**
     * Opens a file for serialization.
     * @param file_name Path name of the file to open.
     * @param access Access mode to use to open the file.
     * @return <code>*this</code>.
     */
    StreamClass& Open(const char *file_name, const char *access)
    {
      result_ = file_.Open(file_name, access);
      return (StreamClass&) (*this);
    }

    /**
     * Closes the file of the serialization and
     * finish the serialization.
     */
    StreamClass& Close()
    {
      file_.Close();
      result_ = false;
      return (StreamClass&) (*this);
    }

    /**
     * Serializes a number of bytes. Depending on the stream operator
     * in the template, this serialization is either a read or a write
     * operation.
     * @param ptr Pointer to the buffer.
     * @param num_bytes Number of bytes.
     * @return <code>*this</code>.
     */
    StreamClass& SerializeBytes(void *ptr, int num_bytes)
    {
      result_ = (result_ && StreamOperator::SerializeBytes(file_, ptr, num_bytes));
      return (StreamClass&) (*this);
    }

    /**
     * This operator overloading is the key of the serialization
     * mechanism.
     */
    template<typename T> StreamClass& operator&(T& var)
    {
      return ((StreamClass&) (*this)).Serialize(var);
    }

    /**
     * Returns the internal serialization status.
     */
    bool result() const
    {
      return result_;
    }

    /**
     * Return the internal serialization status.
     */
    operator bool() const
    {
      return result_;
    }

    /**
     * The destructor automatically closes the file-
     */
    virtual ~BaseStream()
    {
      file_.Close();
    }
  };

  class InputStream;
  class OutputStream;

  /**
   * This template class allows to define a "serializer". By default,
   * the basic serializer calls the method <code>SerializeWith</code>
   * of the objet to be serialized.
   *
   * In order to define a serializer of any other specific type, it is
   * required to define a specialization of this template class, and
   * redefine the methods <code>Load</code> and <code>Save</code>.
   *
   * @see BaseStream
   */
  template<typename T> struct Serializer
  {
    static InputStream& Load(InputStream& stream, T& var)
    {
      return var.SerializeWith(stream);
    }

    static OutputStream& Save(OutputStream& stream, T& var)
    {
      return var.SerializeWith(stream);
    }
  };

  /**
   * Specialization of the <code>BaseStream</code> for input
   * serializations.
   *
   * @see BaseStream
   */
  class InputStream: public BaseStream<InputStream, InputOperator>
  {
  public:
    template<typename T> InputStream& Serialize(T& var)
    {
      return Serializer<T>::Load(*this, var);
    }
  };

  /**
   * Specialization of the <code>BaseStream</code> for output
   * serializations.
   *
   * @see BaseStream
   */
  class OutputStream: public BaseStream<OutputStream, OutputOperator>
  {
  public:
    template<typename T> OutputStream& Serialize(T& var)
    {
      return Serializer<T>::Save(*this, var);
    }
  };

  /**
   * Serializer for the <code>bool</code> type.
   *
   * @see Serializer
   */
  template<> struct Serializer<bool>
  {
    static InputStream& Load(InputStream& stream, bool& var)
    {
      return stream.SerializeBytes(&var, sizeof(var));
    }

    static OutputStream& Save(OutputStream& stream, bool& var)
    {
      return stream.SerializeBytes(&var, sizeof(var));
    }
  };

  /**
   * Serializer for the <code>int</code> type.
   *
   * @see Serializer
   */
  template<> struct Serializer<int>
  {
    static InputStream& Load(InputStream& stream, int& var)
    {
      return stream.SerializeBytes(&var, sizeof(var));
    }

    static OutputStream& Save(OutputStream& stream, int& var)
    {
      return stream.SerializeBytes(&var, sizeof(var));
    }
  };

  /**
   * Serializer for the <code>uint64_t</code> type.
   *
   * @see Serializer
   */
  template<> struct Serializer<uint64_t>
  {
    static InputStream& Load(InputStream& stream, uint64_t& var)
    {
      return stream.SerializeBytes(&var, sizeof(var));
    }

    static OutputStream& Save(OutputStream& stream, uint64_t& var)
    {
      return stream.SerializeBytes(&var, sizeof(var));
    }
  };

  /**
   * Serializer for the <code>string</code> class.
   *
   * @see Serializer
   */
  template<> struct Serializer<string>
  {
    static InputStream& Load(InputStream& stream, string& var)
    {
      int num = 0;

      if (stream.Serialize(num))
      {
        var.clear();

        if (num > 0)
        {
          var.reserve(num);
          char *buf = new char[num];

          if (stream.SerializeBytes(buf, num)) var.append(buf, num);

          delete[] buf;
        }
      }

      return stream;
    }

    static OutputStream& Save(OutputStream& stream, string& var)
    {
      int num = var.size();

      if (stream.Serialize(num)) stream.SerializeBytes((void *) var.c_str(), num);

      return stream;
    }
  };

  /**
   * Serializer for the <code>vector</code> class.
   *
   * @see Serializer
   */
  template<typename T> struct Serializer< vector<T> >
  {
    static InputStream& Load(InputStream& stream, vector<T>& var)
    {
      T item;
      int num = 0;

      var.clear();

      if (stream.Serialize(num))
      {
        for (int i = 0; i < num; i++)
        {
          if (!stream.Serialize(item)) break;
          var.push_back(item);
        }
      }

      return stream;
    }

    static OutputStream& Save(OutputStream& stream, vector<T>& var)
    {
      T item;
      int num = 0;

      num = var.size();

      if (stream.Serialize(num))
      {
        for (int i = 0; i < num; i++)
        {
          item = var[i];
          if (!stream.Serialize(item)) break;
        }
      }

      return stream;
    }
  };

  /**
   * Serializer for the <code>multimap<string,int></code> class.
   *
   * @see Serializer
   */
  template<> struct Serializer< multimap<string, int> >
  {
    static InputStream& Load(InputStream& stream, multimap<string, int>& var)
    {
      string key;
      int value, num = 0;

      var.clear();

      if (stream.Serialize(num))
      {
    	for (int i = 0; i < num; i++)
        {
    	  if ((!stream.Serialize(key)) || (!stream.Serialize(value))) break;
          var.insert(pair<string, int> (key, value));
        }
      }

      return stream;
    }

    static OutputStream& Save(OutputStream& stream, multimap<string, int>& var)
    {
      string key;
      int value, num = 0;

      num = var.size();

      if (stream.Serialize(num))
      {
        for (multimap<string, int>::const_iterator i = var.begin(); i != var.end(); i++)
        {
          key = (*i).first;
          value = (*i).second;
          if ((!stream.Serialize(key)) || (!stream.Serialize(value))) break;
        }
      }

      return stream;
    }
  };
}

#endif /* _DATA_SERIALIZE_H_ */
