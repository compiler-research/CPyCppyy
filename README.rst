.. -*- mode: rst -*-

CPyCppyy: Python-C++ bindings interface based on Cling/LLVM
===========================================================

CPyCppyy is the CPython equivalent of _cppyy in PyPy.
It provides dynamic Python-C++ bindings by leveraging the Cling C++
interpreter and LLVM.
Details and performance are described in
`this paper <http://conferences.computer.org/pyhpc/2016/papers/5220a027.pdf>`_.

CPyCppyy is a CPython extension module built on top of the same backend API
as PyPy/_cppyy.
It thus requires the installation of the
`cppyy backend <https://pypi.python.org/pypi/cppyy-backend/>`_
for use, which will pull in Cling.
CPython/cppyy and PyPy/cppyy are designed to be compatible, although there
are differences due to the former being reference counted and the latter
being garbage collected, as well as temporary differences due to different
release cycles of the respective projects.

Building with Bazel (experimental)
----------------------------------

CMake is the supported build. A best-effort, non-gating Bazel build is also
provided for the extension module (``libcppyy.so``). A non-gating CI job builds
it against an upstream LLVM 22 release (via ``compiler-research/ci-workflows``'s
``setup-llvm``), so a failure warns rather than blocking a PR.

It expects a sibling checkout layout so the shared ``cppyy_bazel`` module and
the ``CppInterOp`` dependency resolve::

    <parent>/CPyCppyy
    <parent>/cppyy        # provides cppyy/bazel
    <parent>/CppInterOp

Point ``LLVM_DIR`` at an LLVM build or release install tree (LLVM 20-22, one
exposing ``bin/llvm-config``, ``lib/``, ``include/``), then build::

    LLVM_DIR=/path/to/llvm bazelisk build //:solib

The output is ``bazel-bin/libcppyy.so``.

----

Find the cppyy documentation here:
  http://cppyy.readthedocs.io

Change log:
  https://cppyy.readthedocs.io/en/latest/changelog.html

Bug reports/feedback:
  https://github.com/wlav/cppyy/issues
