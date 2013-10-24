#ifndef _DATA_FILE_H_
#define _DATA_FILE_H_


#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

#ifndef _NO_FAST_FILE
	#include <stdio_ext.h>
#endif

#include "tr1_compat.h"


namespace data
{

  using namespace std;


  /**
   * Struct for wrapping the basic <code>FILE</code> locked functions for
   * reading and writing defined in <code>stdio.h</code>.
   *
   * @see File
   */
  struct LockedAccess
  {
    static inline void configure(FILE *file_ptr)
    {
    }

    static inline size_t fwrite(const void *ptr, size_t size, size_t count, FILE *file_ptr)
    {
      return ::fwrite(ptr, size, count, file_ptr);
    }

    static inline size_t fread(void * ptr, size_t size, size_t count, FILE *file_ptr)
    {
      return ::fread(ptr, size, count, file_ptr);
    }

    static inline int fgetc(FILE *file_ptr)
    {
      return ::fgetc(file_ptr);
    }

    static inline int fputc(int c, FILE *file_ptr)
    {
      return ::fputc(c, file_ptr);
    }
  };


  /**
   * Struct for wrapping the basic <code>FILE</code> unlocked functions for
   * reading and writing defined in <code>stdio_exts.h</code>.
   *
   * @see File
   */
  #ifndef _NO_FAST_FILE
  struct UnlockedAccess
  {
    static inline void configure(FILE *file_ptr)
    {
      __fsetlocking(file_ptr, FSETLOCKING_BYCALLER);
    }

    static inline size_t fwrite(const void *ptr, size_t size, size_t count, FILE *file_ptr)
    {
      return ::fwrite_unlocked(ptr, size, count, file_ptr);
    }

    static inline size_t fread(void * ptr, size_t size, size_t count, FILE *file_ptr)
    {
      return ::fread_unlocked(ptr, size, count, file_ptr);
    }

    static inline int fgetc(FILE *file_ptr)
    {
      return ::fgetc_unlocked(file_ptr);
    }

    static inline int fputc(int c, FILE *file_ptr)
    {
      return ::fputc_unlocked(c, file_ptr);
    }
  };
  #endif


  /**
   * This is a wrapper class for the <code>FILE</code> functions that
   * provides all the functionality to handle files safely. It is defined
   * as a template in order to allow to use either the locked or the
   * unlocked API, by means of the structs <code>LockedAccess</code> and
   * <code>UnlockedAccess</code>. The unlocked API is not thread-safe,
   * but it provides faster file operations.
   *
   * @see LockedAccess
   * @see UnlockedAccess
   */
  template<class IO> class BaseFile
  {
  public:
	/**
	 * Safe pointer to this class.
	 */
    typedef SHARED_PTR< BaseFile<IO> > Ptr;


    /**
     * Initialized the internal file pointer to <code>NULL</code>.
     */
    BaseFile()
    {
      file_ptr = NULL;
    }

    /**
     * Returns <code>true</code> if the given file exists. This
     * is a wrapper of the system funcion <code>stat</code>.
     */
    static bool Exists(const char *file_name)
    {
      struct stat file_stat;
      return (stat(file_name, &file_stat) == 0);
    }

    /**
     * Opens a file with a specific access mode.
     * @param file_name Path name of the file to open.
     * @param access Access mode as a <code>fopen</code>
     * compatible format string.
     * @return <code>true</code> if successful.
     */
    bool Open(const char *file_name, const char *access)
    {
      assert(file_ptr == NULL);

      if((file_ptr = fopen(file_name, access)) == NULL) return false;
      else {
        IO::configure(file_ptr);
        return true;
      }
    }

    /**
     * Opens a file with a specific access mode.
     * @param file_name Path name of the file to open.
     * @param access Access mode as a <code>fopen</code>
     * compatible format string.
     * @return <code>true</code> if successful.
     */
    bool Open(const string& file_name, const char *access)
    {
      return Open(file_name.c_str(), access);
    }

    /**
     * Opens a file with a specific access mode given an already
     * opened <code>File</code> object. The descriptor of the opened
     * file is duplicated and re-opened with the access mode given.
     * @param file Opened file.
     * @param access Access mode as a <code>fopen</code>
     * compatible format string.
     * @return <code>true</code> if successful.
     */
    template<class IO2> bool Open(const BaseFile<IO2>& file, const char *access)
    {
      assert((file_ptr == NULL) && file.IsValid());

      int new_fd = -1;

      if((new_fd = dup(file.GetDescriptor())) < 0) return false;
      else {
        if((file_ptr = fdopen(new_fd, access)) == NULL) {
          close(new_fd);
          return false;

        } else {
          IO::configure(file_ptr);
          return true;
        }
      }
    }

    /*
     * Calls the function <code>Open</code> with the <code>"rb"
     * </code> access mode (reading).
     */
    bool OpenForReading(const char *file_name)
    {
      return Open(file_name, "rb");
    }

    /*
     * Calls the function <code>Open</code> with the <code>"rb"
     * </code> access mode (reading).
     */
    bool OpenForReading(const string& file_name)
    {
      return Open(file_name.c_str(), "rb");
    }

    /*
     * Calls the function <code>Open</code> with the <code>"rb"
     * </code> access mode (reading).
     */
    template<class IO2> bool OpenForReading(const BaseFile<IO2>& file)
    {
      return Open(file, "rb");
    }

    /*
     * Calls the function <code>Open</code> with the <code>"wb"
     * </code> access mode (writing).
     */
    bool OpenForWriting(const char *file_name)
    {
      return Open(file_name, "wb");
    }

    /*
     * Calls the function <code>Open</code> with the <code>"wb"
     * </code> access mode (writing).
     */
    bool OpenForWriting(const string& file_name)
    {
      return Open(file_name.c_str(), "wb");
    }

    /*
     * Calls the function <code>Open</code> with the <code>"wb"
     * </code> access mode (writing).
     */
    template<class IO2> bool OpenForWriting(const BaseFile<IO2>& file)
    {
      return Open(file, "wb");
    }

    /**
     * Changes the current position of the file.
     * @param offset Offset to add to the current position.
     * @param origin Origin to use for the change (<code>
     * SEEK_SET</code> by default).
     * @return <code>true</code> if successful.
     */
    bool Seek(int offset, int origin = SEEK_SET) const
    {
      assert(file_ptr != NULL);

      return !fseek(file_ptr, offset, origin);
    }

    /**
     * Closes the file.
     */
    void Close()
    {
      if(file_ptr != NULL) {
        fclose(file_ptr);
        file_ptr = NULL;
      }
    }

    /**
     * Returns the current file position.
     */
    uint64_t GetOffset() const
    {
      assert(file_ptr != NULL);

      return ftell(file_ptr);
    }

    /**
     * Returns the EOF status (<code>feof</code>) of the file.
     */
    int IsEOF() const
    {
      assert(file_ptr != NULL);

      return feof(file_ptr);
    }

    /**
     * Returns the file descriptor.
     */
    int GetDescriptor() const
    {
      assert(file_ptr != NULL);

      return fileno(file_ptr);
    }

    /**
     * Return the current size of the file, without modifying
     * the file position.
     */
    uint64_t GetSize() const
    {
      assert(file_ptr != NULL);

      uint64_t offset = GetOffset();
      Seek(0, SEEK_END);
      uint64_t final_offset = GetOffset();
      Seek(offset, SEEK_SET);

      return final_offset;
    }

    /**
     * Reads a byte from the file.
     */
    int ReadByte() const
    {
      assert(file_ptr != NULL);

      return IO::fgetc(file_ptr);
    }

    /**
     * Reads a value from the file.
     * @param value Pointer to the value where to store.
     * @param num_bytes Number of bytes to read (by default,
     * the size of the value).
     * @return <code>true</code> if successful.
     */
    template<typename T> bool Read(T *value, int num_bytes = sizeof(T)) const
    {
      assert(file_ptr != NULL);

      return (IO::fread((void *) value, num_bytes, 1, file_ptr) == 1);
    }

    /**
     * Reads a value from the file in reverse order.
     * @param value Pointer to the value where to store.
     * @param num_bytes Number of bytes to read (by default,
     * the size of the value).
     * @return <code>true</code> if successful.
     */
    template<typename T> bool ReadReverse(T *value, int num_bytes = sizeof(T)) const
    {
      assert(file_ptr != NULL);

      for (char *ptr = ((char *) value) + (num_bytes - 1); num_bytes-- > 0; ptr--)
        if (IO::fread((void *) ptr, 1, 1, file_ptr) != 1) return false;

      return true;
    }

    /**
     * Writes a byte to the file.
     */
    int WriteByte(int c) const
    {
      assert(file_ptr != NULL);

      return IO::fputc(c, file_ptr);
    }

    /**
     * Writes a value to the file.
     * @param value Pointer to the value.
     * @param num_bytes Number of bytes to write (by default,
     * the size of the value).
     * @return <code>true</code> if successful.
     */
    template<typename T> bool Write(T *value, int num_bytes = sizeof(T)) const
    {
      assert(file_ptr != NULL);

      return (IO::fwrite((void *) value, num_bytes, 1, file_ptr) == 1);
    }

    /**
     * Writes a value to the file in reverse order.
     * @param value Pointer to the value.
     * @param num_bytes Number of bytes to write (by default,
     * the size of the value).
     * @return <code>true</code> if successful.
     */
    template<typename T> bool WriteReverse(T *value, int num_bytes = sizeof(T)) const
    {
      assert(file_ptr != NULL);

      for (char *ptr = ((char *) value) + (num_bytes - 1); num_bytes-- > 0; ptr--)
        if (IO::fwrite((void *) ptr, 1, 1, file_ptr) != 1) return false;

      return true;
    }

    /**
     * Returns <code>true</code> if the file pointer is not
     * <code>NULL</code>.
     */
    bool IsValid() const
    {
      return (file_ptr != NULL);
    }

    /**
     * Returns <code>true</code> if the file pointer is not
     * <code>NULL</code>.
     */
    operator bool() const
    {
      return (file_ptr != NULL);
    }

    /**
     * The destructor closes the file.
     */
    virtual ~BaseFile()
    {
      Close();
    }

  private:
    /**
     * File pointer.
     */
    FILE *file_ptr;
  };


  /**
   * Specialization of the class <code>BaseFile</code> with
   * locked access.
   *
   * @see BaseFile
   * @see LockedAccess
   */
  typedef BaseFile<LockedAccess> File;


  /**
   * Specialization of the class <code>BaseFile</code> with
   * unlocked access.
   *
   * @see BaseFile
   * @see UnlockedAccess
   */
	#ifndef _NO_FAST_FILE
  typedef BaseFile<UnlockedAccess> FastFile;
	#endif

}

#endif /* _DATA_FILE_H_ */
