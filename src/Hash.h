#pragma once

#include <pybind11/pybind11.h>

namespace py = pybind11;

#if defined(_MSC_VER)

typedef int int32_t;
typedef unsigned int uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

#else // defined(_MSC_VER)

#include <stdint.h>

#if defined(SUPPORT_INT128)

typedef unsigned __int128 uint128_t;

#define U128_LO(v) (v >> 64)
#define U128_HI(v) (v & 0xFFFFFFFFFFFFFFFF)

#define U128_NEW(LO, HI) ((((uint128_t)HI) << 64) + LO)

#endif // defined(SUPPORT_INT128)

#endif // defined(_MSC_VER)

namespace internal
{
template <typename T>
py::handle convert(const T &value);

template <>
inline py::handle convert(const int &value)
{
  return ::PyLong_FromLong(value);
}

template <>
inline py::handle convert(const unsigned int &value)
{
  return ::PyLong_FromSize_t(value);
}

template <>
inline py::handle convert(const long &value)
{
  return ::PyLong_FromLong(value);
}

template <>
inline py::handle convert(const unsigned long &value)
{
  return ::PyLong_FromUnsignedLong(value);
}

template <>
inline py::handle convert(const long long &value)
{
  return ::PyLong_FromLongLong(value);
}

template <>
inline py::handle convert(const unsigned long long &value)
{
  return ::PyLong_FromUnsignedLongLong(value);
}

#ifndef _MSC_VER
template <>
inline py::handle convert(const uint128_t &value)
{
  return ::_PyLong_FromByteArray((const unsigned char *)&value, sizeof(uint128_t), /*little_endian*/ 1, /*is_signed*/ 0);
}
#endif

template <typename T>
inline T extract_hash_value(PyObject *obj)
{
  T value = 0;

  if (PyLong_Check(obj))
  {
    value = PyLong_AsUnsignedLong(obj);
  }
#if PY_MAJOR_VERSION < 3
  else if (PyInt_Check(obj))
  {
    value = PyInt_AsUnsignedLongMask(obj);
  }
#endif
  else
  {
    ::PyErr_SetString(::PyExc_TypeError, "unknown `seed` type, expected `int` or `long`");
  }

  return value;
}

template <>
inline uint64_t extract_hash_value<uint64_t>(PyObject *obj)
{
  uint64_t value = 0;

  if (PyLong_Check(obj))
  {
    value = PyLong_AsUnsignedLongLong(obj);
  }
#if PY_MAJOR_VERSION < 3
  else if (PyInt_Check(obj))
  {
    value = PyInt_AsUnsignedLongLongMask(obj);
  }
#endif
  else
  {
    ::PyErr_SetString(::PyExc_TypeError, "unknown `seed` type, expected `int` or `long`");
  }

  return value;
}

#if defined(SUPPORT_INT128)
template <>
inline uint128_t extract_hash_value<uint128_t>(PyObject *obj)
{
  uint128_t value = {0};

  if (PyLong_Check(obj))
  {
    _PyLong_AsByteArray((PyLongObject *)obj, (unsigned char *)&value, sizeof(uint128_t), /*little_endian*/ 1, /*is_signed*/ 0);
  }
  else
  {
    ::PyErr_SetString(::PyExc_TypeError, "unknown `seed` type, expected `long`");
  }

  return value;
}
#endif

} // namespace internal

template <typename T>
class Hasher
{
protected:
  Hasher(void) {}

public:
  virtual ~Hasher(void) {}

  static py::object CallWithArgs(py::args args, py::kwargs kwargs);

  static void Export(const py::module &m, const char *name);
};

template <typename T>
inline void Hasher<T>::Export(const py::module &m, const char *name)
{
  py::class_<T>(m, name)
      .def(py::init<>())
      .def("__call__", &T::CallWithArgs);
}

template <typename T>
inline py::object Hasher<T>::CallWithArgs(py::args args, py::kwargs kwargs)
{
  if (args.size() == 0)
    throw std::invalid_argument("missed self argument");

  py::object self = args[0];

  if (!self)
    throw std::invalid_argument("wrong type of self argument");

  const T &hasher = self.cast<T>();
  typename T::hash_value_t value = {0};

  if (kwargs.contains("seed"))
    value = internal::extract_hash_value<typename T::hash_value_t>(kwargs["seed"].ptr());

  std::for_each(std::next(args.begin()), args.end(), [&](const py::handle &arg) {
#if PY_MAJOR_VERSION < 3
    if (PyString_CheckExact(arg.ptr()))
    {
      char *buf = NULL;
      Py_ssize_t len = 0;

      if (0 == PyString_AsStringAndSize(arg.ptr(), &buf, &len))
      {
        value = hasher(buf, len, value);
      }
    }
#else
    if (PyBytes_CheckExact(arg.ptr()))
    {
      char *buf = NULL;
      Py_ssize_t len = 0;

      if (0 == PyBytes_AsStringAndSize(arg.ptr(), &buf, &len))
      {
        value = hasher((void *)buf, len, value);
      }
    }
#endif
    else if (PyUnicode_CheckExact(arg.ptr()))
    {
#if PY_MAJOR_VERSION > 2
#ifdef Py_UNICODE_WIDE
      py::object utf16 = py::reinterpret_borrow<py::object>(PyUnicode_AsUTF16String(arg.ptr()));

      char *buf = NULL;
      Py_ssize_t len = 0;

      if (0 == PyBytes_AsStringAndSize(utf16.ptr(), &buf, &len))
      {
        buf += 2;
        len -= 2;
      }
#else
      Py_UCS2 *buf = PyUnicode_2BYTE_DATA(arg.ptr());
      Py_ssize_t len = PyUnicode_GET_LENGTH(arg.ptr()) * 2;
#endif
#else
#ifdef Py_UNICODE_WIDE
      py::object utf16 = py::reinterpret_borrow<py::object>(PyUnicode_AsUTF16String(arg.ptr()));

      const char *buf = PyString_AS_STRING(utf16.ptr()) + 2; // skip the BOM
      Py_ssize_t len = PyString_GET_SIZE(utf16.ptr()) - 2;
#else
      const char *buf = PyUnicode_AS_DATA(arg.ptr());
      Py_ssize_t len = PyUnicode_GET_DATA_SIZE(arg.ptr());
#endif
#endif

      value = hasher((void *)buf, len, value);
    }
#if PY_MAJOR_VERSION < 3
    else if (PyBuffer_Check(arg.ptr()))
    {
      const void *buf = NULL;
      Py_ssize_t len = 0;

      if (0 == PyObject_AsReadBuffer(arg.ptr(), &buf, &len))
      {
        value = hasher((void *)buf, len, value);
      }
    }
#endif
    else
    {
      throw std::invalid_argument("wrong type of argument");
    }
  });

  return py::reinterpret_steal<py::object>(internal::convert(value));
}
