#ifndef CPYCPPYY_MARSHALSCALAR_H
#define CPYCPPYY_MARSHALSCALAR_H

// Single source of truth for scalar marshaling between Python and C++.
// Each specialization signals failure via Python's C-API convention
// (NULL / -1 + PyErr_Occurred) -- never a C++ throw -- so the contract
// is noexcept and callers can strip exception-handling plumbing.

#include "Cppyy.h"  // for PY_LONG_DOUBLE
#include <Python.h>
#include <climits>
#include <cstdint>

namespace CPyCppyy { extern PyObject* gDefaultObject; }

namespace CPyCppyy {

// Primary templates deliberately undefined: an unsupported T at a
// callsite fails to link with a recognizable symbol name. The Strict
// variant matches the slow-path SetArg/ToMemory policy (range-checked,
// rejects float-to-int coercion); the non-Strict pair is for callsites
// that have already validated.
template <class T> PyObject* MarshalToPy(const T& v) noexcept;
template <class T> bool      MarshalFromPy(PyObject* o, T& out) noexcept;
template <class T> bool      MarshalFromPyStrict(PyObject* o, T& out) noexcept;

// --- bool ------------------------------------------------------------------
template <> inline PyObject* MarshalToPy<bool>(const bool& v) noexcept {
  return PyBool_FromLong(v);
}
template <> inline bool MarshalFromPy<bool>(PyObject* o, bool& out) noexcept {
  int r = PyObject_IsTrue(o);
  if (r < 0) return false;
  out = (r != 0);
  return true;
}

// --- int8 / uint8 ----------------------------------------------------------
template <> inline PyObject* MarshalToPy<int8_t>(const int8_t& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<int8_t>(PyObject* o,
                                              int8_t& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = static_cast<int8_t>(v);
  return true;
}

template <> inline PyObject* MarshalToPy<uint8_t>(const uint8_t& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<uint8_t>(PyObject* o,
                                               uint8_t& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = static_cast<uint8_t>(v);
  return true;
}

// --- short family ----------------------------------------------------------
template <> inline PyObject* MarshalToPy<short>(const short& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<short>(PyObject* o,
                                             short& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = static_cast<short>(v);
  return true;
}

template <> inline PyObject*
MarshalToPy<unsigned short>(const unsigned short& v) noexcept {
  return PyLong_FromUnsignedLong(v);
}
template <> inline bool MarshalFromPy<unsigned short>(
    PyObject* o, unsigned short& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = static_cast<unsigned short>(v);
  return true;
}

// --- int / unsigned int ----------------------------------------------------
template <> inline PyObject* MarshalToPy<int>(const int& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<int>(PyObject* o, int& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = static_cast<int>(v);
  return true;
}

template <>
inline PyObject* MarshalToPy<unsigned int>(const unsigned int& v) noexcept {
  return PyLong_FromUnsignedLong(v);
}
template <> inline bool MarshalFromPy<unsigned int>(
    PyObject* o, unsigned int& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = static_cast<unsigned int>(v);
  return true;
}

// --- long / unsigned long --------------------------------------------------
template <> inline PyObject* MarshalToPy<long>(const long& v) noexcept {
  return PyLong_FromLong(v);
}
template <> inline bool MarshalFromPy<long>(PyObject* o, long& out) noexcept {
  long v = PyLong_AsLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = v;
  return true;
}

template <>
inline PyObject* MarshalToPy<unsigned long>(const unsigned long& v) noexcept {
  return PyLong_FromUnsignedLong(v);
}
template <> inline bool MarshalFromPy<unsigned long>(
    PyObject* o, unsigned long& out) noexcept {
  unsigned long v = PyLong_AsUnsignedLong(o);
  if (v == static_cast<unsigned long>(-1) && PyErr_Occurred()) return false;
  out = v;
  return true;
}

// --- long long / unsigned long long ---------------------------------------
template <>
inline PyObject* MarshalToPy<long long>(const long long& v) noexcept {
  return PyLong_FromLongLong(v);
}
template <> inline bool MarshalFromPy<long long>(PyObject* o,
                                                 long long& out) noexcept {
  long long v = PyLong_AsLongLong(o);
  if (v == -1 && PyErr_Occurred()) return false;
  out = v;
  return true;
}

template <> inline PyObject*
MarshalToPy<unsigned long long>(const unsigned long long& v) noexcept {
  return PyLong_FromUnsignedLongLong(v);
}
template <> inline bool MarshalFromPy<unsigned long long>(
    PyObject* o, unsigned long long& out) noexcept {
  unsigned long long v = PyLong_AsUnsignedLongLong(o);
  if (v == static_cast<unsigned long long>(-1) && PyErr_Occurred())
    return false;
  out = v;
  return true;
}

// --- floating-point family -------------------------------------------------
// PyFloat_AsDouble also accepts PyLong via __float__, matching Python's
// numeric tower; the fast path leans on that rather than reject int returns.
template <> inline PyObject* MarshalToPy<float>(const float& v) noexcept {
  return PyFloat_FromDouble(v);
}
template <> inline bool MarshalFromPy<float>(PyObject* o,
                                             float& out) noexcept {
  double v = PyFloat_AsDouble(o);
  if (v == -1.0 && PyErr_Occurred()) return false;
  out = static_cast<float>(v);
  return true;
}

template <> inline PyObject* MarshalToPy<double>(const double& v) noexcept {
  return PyFloat_FromDouble(v);
}
template <> inline bool MarshalFromPy<double>(PyObject* o,
                                              double& out) noexcept {
  double v = PyFloat_AsDouble(o);
  if (v == -1.0 && PyErr_Occurred()) return false;
  out = v;
  return true;
}

// PY_LONG_DOUBLE is typedef'd to long double in Cppyy.h.
template <> inline PyObject*
MarshalToPy<PY_LONG_DOUBLE>(const PY_LONG_DOUBLE& v) noexcept {
  return PyFloat_FromDouble(static_cast<double>(v));
}
template <> inline bool MarshalFromPy<PY_LONG_DOUBLE>(
    PyObject* o, PY_LONG_DOUBLE& out) noexcept {
  double v = PyFloat_AsDouble(o);
  if (v == -1.0 && PyErr_Occurred()) return false;
  out = static_cast<PY_LONG_DOUBLE>(v);
  return true;
}


// --- MarshalFromPyStrict specializations -----------------------------------
// gDefaultObject is cppyy's "use the default" sentinel; silently coerce
// to zero rather than raise (matches the legacy PyLong_As* validators).

namespace detail {
template <class T>
inline bool ToIntStrict(PyObject* o, T& out, long lo, long hi) noexcept {
    if (!PyLong_Check(o)) {
        if (o == CPyCppyy::gDefaultObject) { out = (T)0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    long l = PyLong_AsLong(o);
    if (l == -1 && PyErr_Occurred()) return false;
    if (l < lo || hi < l) {
        PyErr_Format(PyExc_ValueError,
                     "integer %ld out of destination range", l);
        return false;
    }
    out = static_cast<T>(l);
    return true;
}
} // namespace detail

template <> inline bool
MarshalFromPyStrict<bool>(PyObject* o, bool& out) noexcept {
    long l = PyLong_AsLong(o);
    if (l == -1 && PyErr_Occurred()) return false;
    if ((l != 0 && l != 1) || PyFloat_Check(o)) {
        PyErr_SetString(PyExc_ValueError,
                        "boolean value should be bool, or integer 1 or 0");
        return false;
    }
    out = (l != 0);
    return true;
}

template <> inline bool
MarshalFromPyStrict<int8_t>(PyObject* o, int8_t& out) noexcept {
    return detail::ToIntStrict(o, out, SCHAR_MIN, SCHAR_MAX);
}
template <> inline bool
MarshalFromPyStrict<uint8_t>(PyObject* o, uint8_t& out) noexcept {
    return detail::ToIntStrict(o, out, 0, UCHAR_MAX);
}
template <> inline bool
MarshalFromPyStrict<short>(PyObject* o, short& out) noexcept {
    return detail::ToIntStrict(o, out, SHRT_MIN, SHRT_MAX);
}
template <> inline bool
MarshalFromPyStrict<unsigned short>(PyObject* o,
                                    unsigned short& out) noexcept {
    return detail::ToIntStrict(o, out, 0, USHRT_MAX);
}
template <> inline bool
MarshalFromPyStrict<int>(PyObject* o, int& out) noexcept {
    return detail::ToIntStrict(o, out, INT_MIN, INT_MAX);
}
template <> inline bool
MarshalFromPyStrict<unsigned int>(PyObject* o,
                                  unsigned int& out) noexcept {
    return detail::ToIntStrict(o, out, 0, UINT_MAX);
}

template <> inline bool
MarshalFromPyStrict<long>(PyObject* o, long& out) noexcept {
    if (!PyLong_Check(o)) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    long l = PyLong_AsLong(o);
    if (l == -1 && PyErr_Occurred()) return false;
    out = l;
    return true;
}
template <> inline bool
MarshalFromPyStrict<unsigned long>(PyObject* o,
                                   unsigned long& out) noexcept {
    if (!PyLong_Check(o)) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    unsigned long v = PyLong_AsUnsignedLong(o);
    if (v == (unsigned long)-1 && PyErr_Occurred()) return false;
    out = v;
    return true;
}

template <> inline bool
MarshalFromPyStrict<long long>(PyObject* o, long long& out) noexcept {
    if (!PyLong_Check(o)) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    long long v = PyLong_AsLongLong(o);
    if (v == -1 && PyErr_Occurred()) return false;
    out = v;
    return true;
}
template <> inline bool
MarshalFromPyStrict<unsigned long long>(PyObject* o,
                                        unsigned long long& out) noexcept {
    if (!PyLong_Check(o)) {
        if (o == CPyCppyy::gDefaultObject) { out = 0; return true; }
        PyErr_SetString(PyExc_TypeError,
                        "integer object expected for marshaling");
        return false;
    }
    unsigned long long v = PyLong_AsUnsignedLongLong(o);
    if (v == (unsigned long long)-1 && PyErr_Occurred()) return false;
    out = v;
    return true;
}

// Float family: Python's PyFloat_AsDouble already accepts numeric tower
// (PyLong via __float__); strict() matches PyFloat_AsDouble behavior --
// no extra rejection.
template <> inline bool
MarshalFromPyStrict<float>(PyObject* o, float& out) noexcept {
    double v = PyFloat_AsDouble(o);
    if (v == -1.0 && PyErr_Occurred()) {
        if (o == CPyCppyy::gDefaultObject) { PyErr_Clear(); out = 0; return true; }
        return false;
    }
    out = static_cast<float>(v);
    return true;
}
template <> inline bool
MarshalFromPyStrict<double>(PyObject* o, double& out) noexcept {
    double v = PyFloat_AsDouble(o);
    if (v == -1.0 && PyErr_Occurred()) {
        if (o == CPyCppyy::gDefaultObject) { PyErr_Clear(); out = 0; return true; }
        return false;
    }
    out = v;
    return true;
}
template <> inline bool
MarshalFromPyStrict<PY_LONG_DOUBLE>(PyObject* o,
                                    PY_LONG_DOUBLE& out) noexcept {
    double v = PyFloat_AsDouble(o);
    if (v == -1.0 && PyErr_Occurred()) {
        if (o == CPyCppyy::gDefaultObject) { PyErr_Clear(); out = 0; return true; }
        return false;
    }
    out = static_cast<PY_LONG_DOUBLE>(v);
    return true;
}

} // namespace CPyCppyy

#endif // CPYCPPYY_MARSHALSCALAR_H
