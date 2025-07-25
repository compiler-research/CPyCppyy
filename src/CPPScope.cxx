// Bindings
#include "CPyCppyy.h"
#include "CPPScope.h"
#include "CPPDataMember.h"
#include "CPPEnum.h"
#include "CPPFunction.h"
#include "CPPOverload.h"
#include "CustomPyTypes.h"
#include "Dispatcher.h"
#include "ProxyWrappers.h"
#include "PyStrings.h"
#include "TemplateProxy.h"
#include "TypeManip.h"
#include "Utility.h"

// Standard
#include <string.h>
#include <algorithm>         // for for_each
#include <set>
#include <string>
#include <vector>


namespace CPyCppyy {

extern PyTypeObject CPPInstance_Type;

//- helpers ------------------------------------------------------------------
static inline PyObject* add_template(PyObject* pyclass,
    const std::string& name, std::vector<PyCallable*>* overloads = nullptr)
{
// If templated, the user-facing function must be the template proxy, but the
// specific lookup must be the current overload, if already found.

    const std::string& ncl = TypeManip::clean_type(name);
    PyObject* pyncl = CPyCppyy_PyText_FromString(ncl.c_str());
    TemplateProxy* pytmpl = (TemplateProxy*)PyType_Type.tp_getattro(pyclass, pyncl);
    if (!pytmpl) {
        PyErr_Clear();
        pytmpl = TemplateProxy_New(ncl, ncl, pyclass);
    // cache the template on its clean name
        PyType_Type.tp_setattro(pyclass, pyncl, (PyObject*)pytmpl);
        Py_DECREF(pyncl);
    } else if (!TemplateProxy_CheckExact((PyObject*)pytmpl)) {
        Py_DECREF(pytmpl);
        return nullptr;
    }

    if (overloads) {
    // adopt the new overloads
        if (ncl != name)
            for (auto clb : *overloads) pytmpl->AdoptTemplate(clb);
        else
            for (auto clb : *overloads) pytmpl->AdoptMethod(clb);
    }

// the caller expects a method matching the full name, thus is a specialization
// was requested, do not return the template yet
    if (ncl == name)
        return (PyObject*)pytmpl;

    Py_DECREF(pytmpl);
    return nullptr;       // so that caller caches the method on full name
}


//= CPyCppyy type proxy construction/destruction =============================
static PyObject* meta_alloc(PyTypeObject* meta, Py_ssize_t nitems)
{
// pure memory allocation; object initialization is in pt_new
    return PyType_Type.tp_alloc(meta, nitems);
}

//----------------------------------------------------------------------------
static void meta_dealloc(CPPScope* scope)
{
    if (scope->fFlags & CPPScope::kIsNamespace) {
        if (scope->fImp.fUsing) {
            for (auto pyobj : *scope->fImp.fUsing) Py_DECREF(pyobj);
            delete scope->fImp.fUsing; scope->fImp.fUsing = nullptr;
        }
    } else if (!(scope->fFlags & CPPScope::kIsPython)) {
        delete scope->fImp.fCppObjects; scope->fImp.fCppObjects = nullptr;
    }
    delete scope->fOperators;
    free(scope->fModuleName);
    return PyType_Type.tp_dealloc((PyObject*)scope);
}

//-----------------------------------------------------------------------------
static PyObject* meta_getcppname(CPPScope* scope, void*)
{
    if ((void*)scope == (void*)&CPPInstance_Type)
        return CPyCppyy_PyText_FromString("CPPInstance_Type");
    return CPyCppyy_PyText_FromString(Cppyy::GetScopedFinalName(scope->fCppType).c_str());
}

//-----------------------------------------------------------------------------
static PyObject* meta_getmodule(CPPScope* scope, void*)
{
    if ((void*)scope == (void*)&CPPInstance_Type)
        return CPyCppyy_PyText_FromString("cppyy.gbl");

    if (scope->fModuleName)
        return CPyCppyy_PyText_FromString(scope->fModuleName);

// get C++ representation of outer scope
    Cppyy::TCppScope_t parent_scope = Cppyy::GetParentScope(scope->fCppType);
    if (parent_scope == Cppyy::GetGlobalScope())
        return CPyCppyy_PyText_FromString(const_cast<char*>("cppyy.gbl"));

// now peel scopes one by one, pulling in the python naming (which will
// simply recurse if not overridden in python)
    PyObject* pymodule = nullptr;
    PyObject* pyscope = CPyCppyy::GetScopeProxy(parent_scope);
    if (pyscope) {
    // get the module of our module
        pymodule = PyObject_GetAttr(pyscope, PyStrings::gModule);
        if (pymodule) {
        // append name of our module
            PyObject* pymodname = PyObject_GetAttr(pyscope, PyStrings::gName);
            if (pymodname) {
                CPyCppyy_PyText_AppendAndDel(&pymodule, CPyCppyy_PyText_FromString("."));
                CPyCppyy_PyText_AppendAndDel(&pymodule, pymodname);
            }
        }
        Py_DECREF(pyscope);
    }

    if (pymodule)
        return pymodule;
    PyErr_Clear();

// lookup through python failed, so simply cook up a '::' -> '.' replacement
    std::string modname = Cppyy::GetScopedFinalName(parent_scope);
    TypeManip::cppscope_to_pyscope(modname);
    return CPyCppyy_PyText_FromString(("cppyy.gbl."+modname).c_str());
}

//-----------------------------------------------------------------------------
static int meta_setmodule(CPPScope* scope, PyObject* value, void*)
{
    if ((void*)scope == (void*)&CPPInstance_Type) {
        PyErr_SetString(PyExc_AttributeError,
            "attribute \'__module__\' of 'cppyy.CPPScope\' objects is not writable");
        return -1;
    }

    const char* newname = CPyCppyy_PyText_AsStringChecked(value);
    if (!value)
        return -1;

    free(scope->fModuleName);
    Py_ssize_t sz = CPyCppyy_PyText_GET_SIZE(value);
    scope->fModuleName = (char*)malloc(sz+1);
    memcpy(scope->fModuleName, newname, sz+1);

    return 0;
}

//----------------------------------------------------------------------------
static PyObject* meta_repr(CPPScope* scope)
{
// Specialized b/c type_repr expects __module__ to live in the dictionary,
// whereas it is a property (to save memory).
    if ((void*)scope == (void*)&CPPInstance_Type)
        return CPyCppyy_PyText_FromFormat(
            const_cast<char*>("<class cppyy.CPPInstance at %p>"), scope);

    if (scope->fFlags & (CPPScope::kIsMeta | CPPScope::kIsPython)) {
    // either meta type or Python-side derived class: use default type printing
        return PyType_Type.tp_repr((PyObject*)scope);
    }

// skip in case some Python-side derived meta class
    if (!CPPScope_Check(scope) || !scope->fCppType)
        return PyType_Type.tp_repr((PyObject*)scope);

// printing of C++ classes
    PyObject* modname = meta_getmodule(scope, nullptr);
    std::string clName = Cppyy::GetFinalName(scope->fCppType);
    const char* kind = (scope->fFlags & CPPScope::kIsNamespace) ? "namespace" : "class";

    PyObject* repr = CPyCppyy_PyText_FromFormat("<%s %s.%s at %p>",
        kind, CPyCppyy_PyText_AsString(modname), clName.c_str(), scope);

    Py_DECREF(modname);
    return repr;
}


//= CPyCppyy type metaclass behavior =========================================
static PyObject* pt_new(PyTypeObject* subtype, PyObject* args, PyObject* kwds)
{
// Called when CPPScope acts as a metaclass; since type_new always resets
// tp_alloc, and since it does not call tp_init on types, the metaclass is
// being fixed up here, and the class is initialized here as well.

// fixup of metaclass (left permanent, and in principle only called once b/c
// cppyy caches python classes)
    subtype->tp_alloc   = (allocfunc)meta_alloc;
    subtype->tp_dealloc = (destructor)meta_dealloc;

// creation of the python-side class; extend the size if this is a smart ptr
    Cppyy::TCppType_t raw{0}; Cppyy::TCppMethod_t deref{0};
    if (CPPScope_CheckExact(subtype)) {
        if (Cppyy::GetSmartPtrInfo(Cppyy::GetScopedFinalName(((CPPScope*)subtype)->fCppType), &raw, &deref))
            subtype->tp_basicsize = sizeof(CPPSmartClass);
    }

    CPPScope* result = (CPPScope*)PyType_Type.tp_new(subtype, args, kwds);
    if (!CPPScope_Check(result)) {
    // either failed or custom user-side metaclass that can't be handled here
        return nullptr;
    }

    result->fFlags      = CPPScope::kNone;
    result->fOperators  = nullptr;
    result->fModuleName = nullptr;

    if (raw && deref) {
        result->fFlags |= CPPScope::kIsSmart;
        ((CPPSmartClass*)result)->fUnderlyingType = raw;
        ((CPPSmartClass*)result)->fDereferencer   = deref;
    }

// initialization of class (based on metatype)
    if (!CPPScope_CheckExact(subtype) || !strstr(subtype->tp_name, "_meta") /* convention */) {
    // there has been a user meta class override in a derived class, so do
    // the consistent thing, thus allowing user control over naming
        result->fCppType = Cppyy::GetScope(
            CPyCppyy_PyText_AsString(PyTuple_GET_ITEM(args, 0)));
    } else {
    // coming here from cppyy or from sub-classing in python; take the
    // C++ type from the meta class to make sure that the latter category
    // has fCppType properly set (it inherits the meta class, but has an
    // otherwise unknown (or wrong) C++ type)
        result->fCppType = ((CPPScope*)subtype)->fCppType;

    // the following is not robust, but by design, C++ classes get their
    // dictionaries filled after creation (chicken & egg problem as they
    // can return themselves in methods), whereas a derived Python class
    // with method overrides will have a non-empty dictionary (even if it
    // has no methods, it will at least have a module name)
        if (3 <= PyTuple_GET_SIZE(args)) {
            PyObject* dct = PyTuple_GET_ITEM(args, 2);
            Py_ssize_t sz = PyDict_Size(dct);
            if (0 < sz && !Cppyy::IsNamespace(result->fCppType)) {
                result->fFlags |= CPPScope::kIsPython;
                if (1 < PyTuple_GET_SIZE(PyTuple_GET_ITEM(args, 1)))
                    result->fFlags |= CPPScope::kIsMultiCross;
                std::ostringstream errmsg;
                if (!InsertDispatcher(result, PyTuple_GET_ITEM(args, 1), dct, errmsg)) {
                    PyErr_Format(PyExc_TypeError, "no python-side overrides supported (%s)", errmsg.str().c_str());
                    return nullptr;
                } else {
                // the direct base can be useful for some templates, such as shared_ptrs,
                // so make it accessible (the __cpp_cross__ data member also signals that
                // this is a cross-inheritance class)
                    PyObject* bname = CPyCppyy_PyText_FromString(Cppyy::GetBaseName(result->fCppType, 0).c_str());
                    if (PyObject_SetAttrString((PyObject*)result, "__cpp_cross__", bname) == -1)
                        PyErr_Clear();
                    Py_DECREF(bname);
                }
            } else if (sz == (Py_ssize_t)-1)
                PyErr_Clear();
        }
    }

// if the user messed with the metaclass, then we may not have a C++ type,
// simply return here before more damage gets done
    if (!result->fCppType)
        return (PyObject*)result;

// maps for using namespaces and tracking objects
    if (!Cppyy::IsNamespace(result->fCppType)) {
        static Cppyy::TCppType_t exc_type = (Cppyy::TCppType_t)Cppyy::GetScope("exception", Cppyy::GetScope("std"));
        if (Cppyy::IsSubclass(result->fCppType, exc_type))
            result->fFlags |= CPPScope::kIsException;
        if (!(result->fFlags & CPPScope::kIsPython))
            result->fImp.fCppObjects = new CppToPyMap_t;
        else {
        // special case: the C++ objects should be stored with the associated C++, not Python, type
            CPPClass* kls = (CPPClass*)GetScopeProxy(result->fCppType);
            if (kls) {
                result->fImp.fCppObjects = kls->fImp.fCppObjects;
                Py_DECREF(kls);
            } else
                result->fImp.fCppObjects = nullptr;
        }
    } else {
        result->fImp.fUsing = nullptr;
        result->fFlags |= CPPScope::kIsNamespace;
    }

    if (PyErr_Occurred()) {
        Py_DECREF((PyObject*)result);
        return nullptr;
    }

    return (PyObject*)result;
}


//----------------------------------------------------------------------------
static PyObject* meta_getattro(PyObject* pyclass, PyObject* pyname)
{
// normal type-based lookup
    PyObject* attr = PyType_Type.tp_getattro(pyclass, pyname);
    if (pyclass == (PyObject*)&CPPInstance_Type)
        return attr;

    PyObject* possibly_shadowed = nullptr;
    if (attr) {
        if (CPPScope_Check(attr) && CPPScope_Check(pyclass) && !(((CPPScope*)pyclass)->fFlags & CPPScope::kIsNamespace)) {
        // TODO: the goal here is to prevent typedefs that are shadowed in subclasses
        // to be found as from the base class. The better approach would be to find
        // all typedefs at class creation and insert placeholders to flag here. The
        // current typedef loop in gInterpreter won't do, however.
            PyObject* dct = PyObject_GetAttr(pyclass, PyStrings::gDict);
            if (dct) {
                PyObject* attr_from_dict = PyObject_GetItem(dct, pyname);
                Py_DECREF(dct);
                if (attr_from_dict) {
                    Py_DECREF(attr);
                    return attr_from_dict;
                }
                possibly_shadowed = attr;
                attr = nullptr;
            }
            PyErr_Clear();
        } else if (CPPScope_Check(attr) && CPPScope_Check(pyclass) && ((CPPScope*)attr)->fFlags & CPPScope::kIsException) {
            return CreateExcScopeProxy(attr, pyname, pyclass);
        } else
            return attr;
    }

    if (!CPyCppyy_PyText_CheckExact(pyname) || !CPPScope_Check(pyclass))
        return possibly_shadowed;

// filter for python specials
    std::string name = CPyCppyy_PyText_AsString(pyname);
    if (name.size() >= 5 && name.compare(0, 2, "__") == 0 &&
            name.compare(name.size()-2, name.size(), "__") == 0)
        return possibly_shadowed;

// more elaborate search in case of failure (eg. for inner classes on demand)
    std::vector<Utility::PyError_t> errors;
    Utility::FetchError(errors);
    attr = CreateScopeProxy(name, pyclass);

    if (CPPScope_Check(attr) && (((CPPScope*)attr)->fFlags & CPPScope::kIsException)) {
    // Instead of the CPPScope, return a fresh exception class derived from CPPExcInstance.
        return CreateExcScopeProxy(attr, pyname, pyclass);
    }

    bool templated_functions_checked = false;
    CPPScope* klass = ((CPPScope*)pyclass);
    if (!attr) {
        Utility::FetchError(errors);
        Cppyy::TCppScope_t scope = klass->fCppType;

    // namespaces may have seen updates in their list of global functions, which
    // are available as "methods" even though they're not really that
        if ((klass->fFlags & CPPScope::kIsNamespace) || 
                scope == Cppyy::GetGlobalScope()) {
        // tickle lazy lookup of functions
            const std::vector<Cppyy::TCppScope_t> methods =
                Cppyy::GetMethodsFromName(scope, name);
            if (!methods.empty()) {
            // function exists, now collect overloads
                std::vector<PyCallable*> overloads;
                for (auto method : methods) {
                    overloads.push_back(new CPPFunction(scope, method));
                }

            // Note: can't re-use Utility::AddClass here, as there's the risk of
            // a recursive call. Simply add method directly, as we're guaranteed
            // that it doesn't exist yet.
                if (Cppyy::ExistsMethodTemplate(scope, name))
                    attr = add_template(pyclass, name, &overloads);
                else
                    attr = (PyObject*)CPPOverload_New(name, overloads);
                templated_functions_checked = true;
            }
        }

        if (!attr) {
            Cppyy::TCppScope_t lookup_result = Cppyy::GetNamed(name, scope);
            if (Cppyy::IsVariable(lookup_result) || Cppyy::IsEnumConstant(lookup_result)) {
                attr = (PyObject*)CPPDataMember_New(scope, lookup_result);
            } else if (Cppyy::IsTypedefed(lookup_result)) {
                Cppyy::TCppType_t resolved_type = Cppyy::ResolveType(Cppyy::GetTypeFromScope(lookup_result));
                const std::string& cpd = TypeManip::compound(Cppyy::GetTypeAsString(resolved_type));
                if (cpd == "*") {
                    Cppyy::TCppScope_t tcl = Cppyy::GetScopeFromType(Cppyy::ResolveType(resolved_type));
                    if (tcl) {
                        typedefpointertoclassobject* tpc =
                            PyObject_GC_New(typedefpointertoclassobject, &TypedefPointerToClass_Type);
                        tpc->fCppType = tcl;
                        tpc->fDict = PyDict_New();
                        attr = (PyObject*)tpc;
                    }
                }
            }
        }

    // function templates that have not been instantiated (namespaces _may_ have already
    // been taken care of, by their general function lookup above)
        if (!attr && !templated_functions_checked) {
            if (Cppyy::ExistsMethodTemplate(scope, name))
                attr = add_template(pyclass, name);
            else {
            // for completeness in error reporting
                PyErr_Format(PyExc_TypeError, "\'%s\' is not a known C++ template", name.c_str());
                Utility::FetchError(errors);
            }
        }

    // enums types requested as type (rather than the constants)
        if (!attr) {
            Cppyy::TCppScope_t enumerator = Cppyy::GetUnderlyingScope(Cppyy::GetNamed(name, scope));
            if (Cppyy::IsEnumScope(enumerator)) {
            // enum types (incl. named and class enums)
                attr = (PyObject*)CPPEnum_New(name, enumerator);
            } else {
            // for completeness in error reporting
                PyErr_Format(PyExc_TypeError, "\'%s\' is not a known C++ enum", name.c_str());
                Utility::FetchError(errors);
            }
        }

        if (attr) {
        // cache the result
            if (CPPDataMember_Check(attr)) {
                PyType_Type.tp_setattro((PyObject*)Py_TYPE(pyclass), pyname, attr);

                Py_DECREF(attr);
                // The call below goes through "dm_get"
                attr = PyType_Type.tp_getattro(pyclass, pyname);
                if (!attr && PyErr_Occurred())
                    Utility::FetchError(errors);
            } else
                PyType_Type.tp_setattro(pyclass, pyname, attr);

        } else {
            Utility::FetchError(errors);
        }
    }

    if (!attr && (klass->fFlags & CPPScope::kIsNamespace)) {
    // refresh using list as necessary
        const std::vector<Cppyy::TCppScope_t>& uv = Cppyy::GetUsingNamespaces(klass->fCppType);
        if (!klass->fImp.fUsing || uv.size() != klass->fImp.fUsing->size()) {
            if (klass->fImp.fUsing) {
                for (auto pyref : *klass->fImp.fUsing) Py_DECREF(pyref);
                klass->fImp.fUsing->clear();
            } else
                klass->fImp.fUsing = new std::vector<PyObject*>;

        // reload and reset weak refs
            for (auto uid : uv) {
                std::string uname = Cppyy::GetScopedFinalName(uid);
                PyObject* pyuscope = CreateScopeProxy(uname);
                if (pyuscope) {
                    klass->fImp.fUsing->push_back(PyWeakref_NewRef(pyuscope, nullptr));
                // the namespace may not otherwise be held, so tie the lifetimes
                    PyObject* llname = CPyCppyy_PyText_FromString(("__lifeline_"+uname).c_str());
                    PyType_Type.tp_setattro(pyclass, llname, pyuscope);
                    Py_DECREF(llname);
                    Py_DECREF(pyuscope);
                }
            }
        }

    // try all outstanding using namespaces in turn to find the attribute (will cache
    // locally later; TODO: doing so may cause pathological cases)
        for (auto pyref : *klass->fImp.fUsing) {
            PyObject* pyuscope = CPyCppyy_GetWeakRef(pyref);
            if (pyuscope) {
                attr = PyObject_GetAttr(pyuscope, pyname);
                if (!attr) PyErr_Clear();
                Py_DECREF(pyuscope);
            }
            if (attr)
                break;
        }
    }

// if the attribute was not found but could possibly have been shadowed, insert it into
// the dict now, to short-circuit future lookups (TODO: this is part of the workaround
// described above of which the true solution is to loop over all typedefs at creation
// time for the class)
    if (possibly_shadowed) {
        if (attr) {
            Py_DECREF(possibly_shadowed);
        } else {
            attr = possibly_shadowed;
            PyType_Type.tp_setattro(pyclass, pyname, attr);
        }
    }

    if (attr) {
        std::for_each(errors.begin(), errors.end(), Utility::PyError_t::Clear);
        PyErr_Clear();
    } else {
    // not found: prepare a full error report
        PyObject* topmsg = nullptr;
        PyObject* pytype = 0, *pyvalue = 0, *pytrace = 0;
        PyErr_Fetch(&pytype, &pyvalue, &pytrace);
        PyObject* sklass = PyObject_Str(pyclass);
        PyErr_Restore(pytype, pyvalue, pytrace);
        if (sklass) {
            topmsg = CPyCppyy_PyText_FromFormat("%s has no attribute \'%s\'. Full details:",
                CPyCppyy_PyText_AsString(sklass), CPyCppyy_PyText_AsString(pyname));
            Py_DECREF(sklass);
        } else {
            topmsg = CPyCppyy_PyText_FromFormat("no such attribute \'%s\'. Full details:",
                CPyCppyy_PyText_AsString(pyname));
        }
        SetDetailedException(errors, topmsg /* steals */, PyExc_AttributeError /* default error */);
    }

    return attr;
}

//----------------------------------------------------------------------------
static int meta_setattro(PyObject* pyclass, PyObject* pyname, PyObject* pyval)
{
// Global data and static data in namespaces is found lazily, thus if the first
// use is setting of the global data by the user, it will not be reflected on
// the C++ side, b/c there is no descriptor yet. This triggers the creation for
// for such data as necessary. The many checks to narrow down the specific case
// are needed to prevent unnecessary lookups and recursion.
    // skip if the given pyval is a descriptor already, or an unassignable class
    if (!CPyCppyy::CPPDataMember_Check(pyval) && !CPyCppyy::CPPScope_Check(pyval)) {
        std::string name = CPyCppyy_PyText_AsString(pyname);
        if (Cppyy::GetNamed(name, ((CPPScope*)pyclass)->fCppType))
            meta_getattro(pyclass, pyname);       // triggers creation
    }

    return PyType_Type.tp_setattro(pyclass, pyname, pyval);
}


//----------------------------------------------------------------------------
static PyObject* meta_reflex(CPPScope* klass, PyObject* args)
{
// Provide the requested reflection information.
    Cppyy::Reflex::RequestId_t request = -1;
    Cppyy::Reflex::FormatId_t  format  = Cppyy::Reflex::OPTIMAL;
    if (!PyArg_ParseTuple(args, const_cast<char*>("i|i:__cpp_reflex__"), &request, &format))
        return nullptr;

    switch (request) {
    case Cppyy::Reflex::IS_NAMESPACE:
        if (klass->fFlags & CPPScope::kIsNamespace)
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
        break;
    case Cppyy::Reflex::IS_AGGREGATE:
      // this is not the strict C++ definition of aggregates, but is closer to what
      // is needed for Numba and C calling conventions (TODO: probably have to check
      // for all public data types, too, and maybe for no padding?)
        if (Cppyy::IsAggregate(klass->fCppType) || !Cppyy::HasVirtualDestructor(klass->fCppType))
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
        break;
    }

    PyErr_Format(PyExc_ValueError, "unsupported reflex request %d or format %d", request, format);
    return nullptr;
}

//----------------------------------------------------------------------------
// p2.7 does not have a __dir__ in object, and object.__dir__ in p3 does not
// quite what I'd expected of it, so the following pulls in the internal code
#include "PyObjectDir27.inc"

static PyObject* meta_dir(CPPScope* klass)
{
// Collect a list of everything (currently) available in the namespace.
// The backend can filter by returning empty strings. Special care is
// taken for functions, which need not be unique (overloading).
    using namespace Cppyy;

    if ((void*)klass == (void*)&CPPInstance_Type)
        return PyList_New(0);

    if (!CPyCppyy::CPPScope_Check((PyObject*)klass)) {
        PyErr_SetString(PyExc_TypeError, "C++ proxy scope expected");
        return nullptr;
    }

    PyObject* dirlist = _generic_dir((PyObject*)klass);
    if (!(klass->fFlags & CPPScope::kIsNamespace))
        return dirlist;

    std::set<std::string> cppnames;
    Cppyy::GetAllCppNames(klass->fCppType, cppnames);

// cleanup names
    std::set<std::string> dir_cppnames;
    for (const std::string& name : cppnames) {
        if (name.find("__", 0, 2) != std::string::npos || \
            name.find("<") != std::string::npos || \
            name.find("operator", 0, 8) != std::string::npos) continue;
        dir_cppnames.insert(name);
    }

// get rid of duplicates
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(dirlist); ++i)
        dir_cppnames.insert(CPyCppyy_PyText_AsString(PyList_GET_ITEM(dirlist, i)));

    Py_DECREF(dirlist);
    dirlist = PyList_New(dir_cppnames.size());

// copy total onto python list
    Py_ssize_t i = 0;
    for (const auto& name : dir_cppnames) {
        PyList_SET_ITEM(dirlist, i++, CPyCppyy_PyText_FromString(name.c_str()));
    }
    return dirlist;
}

//-----------------------------------------------------------------------------
static PyMethodDef meta_methods[] = {
    {(char*)"__cpp_reflex__", (PyCFunction)meta_reflex, METH_VARARGS,
      (char*)"C++ datamember reflection information" },
    {(char*)"__dir__", (PyCFunction)meta_dir, METH_NOARGS, nullptr},
    {(char*)nullptr, nullptr, 0, nullptr}
};


//-----------------------------------------------------------------------------
static PyGetSetDef meta_getset[] = {
    {(char*)"__cpp_name__", (getter)meta_getcppname, nullptr, nullptr, nullptr},
    {(char*)"__module__",   (getter)meta_getmodule,  (setter)meta_setmodule, nullptr, nullptr},
    {(char*)nullptr, nullptr, nullptr, nullptr, nullptr}
};


//= CPyCppyy object proxy type type ==========================================
PyTypeObject CPPScope_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    (char*)"cppyy.CPPScope",       // tp_name
    sizeof(CPyCppyy::CPPScope),    // tp_basicsize
    0,                             // tp_itemsize
    0,                             // tp_dealloc
    0,                             // tp_vectorcall_offset / tp_print
    0,                             // tp_getattr
    0,                             // tp_setattr
    0,                             // tp_as_async / tp_compare
    (reprfunc)meta_repr,           // tp_repr
    0,                             // tp_as_number
    0,                             // tp_as_sequence
    0,                             // tp_as_mapping
    0,                             // tp_hash
    0,                             // tp_call
    0,                             // tp_str
    (getattrofunc)meta_getattro,   // tp_getattro
    (setattrofunc)meta_setattro,   // tp_setattro
    0,                             // tp_as_buffer
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE
#if PY_VERSION_HEX >= 0x03040000
        | Py_TPFLAGS_TYPE_SUBCLASS
#endif
        ,                          // tp_flags
    (char*)"CPyCppyy metatype (internal)",        // tp_doc
    0,                             // tp_traverse
    0,                             // tp_clear
    0,                             // tp_richcompare
    0,                             // tp_weaklistoffset
    0,                             // tp_iter
    0,                             // tp_iternext
    meta_methods,                  // tp_methods
    0,                             // tp_members
    meta_getset,                   // tp_getset
    &PyType_Type,                  // tp_base
    0,                             // tp_dict
    0,                             // tp_descr_get
    0,                             // tp_descr_set
    0,                             // tp_dictoffset
    0,                             // tp_init
    0,                             // tp_alloc
    (newfunc)pt_new,               // tp_new
    0,                             // tp_free
    0,                             // tp_is_gc
    0,                             // tp_bases
    0,                             // tp_mro
    0,                             // tp_cache
    0,                             // tp_subclasses
    0                              // tp_weaklist
#if PY_VERSION_HEX >= 0x02030000
    , 0                            // tp_del
#endif
#if PY_VERSION_HEX >= 0x02060000
    , 0                            // tp_version_tag
#endif
#if PY_VERSION_HEX >= 0x03040000
    , 0                            // tp_finalize
#endif
#if PY_VERSION_HEX >= 0x03080000
    , 0                           // tp_vectorcall
#endif
#if PY_VERSION_HEX >= 0x030c0000
    , 0                           // tp_watched
#endif
#if PY_VERSION_HEX >= 0x030d0000
    , 0                           // tp_versions_used
#endif
};

} // namespace CPyCppyy
