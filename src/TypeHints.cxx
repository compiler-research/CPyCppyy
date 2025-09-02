
#include "CPyCppyy.h"
#include "Cppyy.h"

#include <string>
#include <sstream>
#include <fstream>
#include <algorithm>

inline std::string replace_all(std::string str, const std::string& from, const std::string& to) {
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
    }
    return str;
}

inline std::string trim(const std::string& line)
{
    if (line.empty()) return "";
    const char* WhiteSpace = " \t\v\r\n";
    std::size_t start = line.find_first_not_of(WhiteSpace);
    std::size_t end = line.find_last_not_of(WhiteSpace);
    if (start == std::string::npos) return "";
    return line.substr(start, end - start + 1);
}

inline std::string indentation(int indent = 0) {
    std::ostringstream ostream_indentation;
    for (size_t i = 0; i < indent * 4; i++)
        ostream_indentation << " ";
    return ostream_indentation.str();
}

std::string pythonize_method_name(std::string &method_name, std::string &class_name) {
    if (method_name == class_name)
        return "__init__";
    if (method_name == ("~" + class_name))
        return "__del__";
    if (method_name == "operator=")
        return "";
    if (method_name == "operator+")
        return "__add__";
    if (method_name == "operator-")
        return "__sub__";
    if (method_name == "operator*")
        return "__mul__";
    if (method_name == "operator/")
        return "__div__";
    if (method_name == "operator[]")
        return "__getitem__";
    if (method_name == "operator()")
        return "__call__";
    return method_name;
}
std::string pythonize_type_name(std::string cpptype) {
    // TODO: handle complex numerical type types
    // TODO: change this to Pythonize and replace `<> -> []` && `:: -> .`
    if (cpptype == "short" ||
        cpptype == "unsigned short" ||
        cpptype == "signed char" ||
        cpptype == "unsigned char" ||
        cpptype == "unsigned int" ||
        cpptype == "long" ||
        cpptype == "long long" ||
        cpptype == "long int" ||
        cpptype == "long long int" ||
        cpptype == "unsigned long" ||
        cpptype == "unsigned long long" ||
        cpptype == "unsigned long int" ||
        cpptype == "unsigned long long int"
        ) {
            return "int";
        }
    if (cpptype == "double")
        return "float";
    if (cpptype == "char")
        return "str";
    if (cpptype == "void")
        return "None";
    
    cpptype = replace_all(cpptype, "&", "");
    cpptype = replace_all(cpptype, "const", "");
    cpptype = replace_all(cpptype, "*", "");
    cpptype = trim(cpptype);
    std::string result = replace_all(cpptype, "<", "["); // FIXME: what if the template agruments are short ...
    result = replace_all(result, ">", "]");
    result = replace_all(result, "::", ".");
    if (result != cpptype)
        return "\"" + result + "\"";
    return result;
}

// TODO: extract docs from C++ code
void handle_scope(Cppyy::TCppScope_t typ, std::ostringstream &file, int indent = 0);
void handle_namespace(Cppyy::TCppScope_t ns, std::ostringstream &file, int indent = 0);
void handle_class(Cppyy::TCppScope_t cls, std::ostringstream &file, int indent = 0);
void handle_templates(Cppyy::TCppScope_t tmpl, std::ostringstream &file, int indent = 0);
void handle_function(Cppyy::TCppScope_t fn, std::ostringstream &file, int indent = 0);
void handle_typedef(Cppyy::TCppScope_t var, std::ostringstream &file, int indent = 0);
void handle_variable(Cppyy::TCppScope_t var, std::ostringstream &file, int indent);

void handle_variable(Cppyy::TCppScope_t var, std::ostringstream &file, int indent = 0) {
    std::string indentation_str = indentation(indent);

    file << indentation_str
        << Cppyy::GetFinalName(var)
        << ": "
        << pythonize_type_name(Cppyy::GetTypeAsString(Cppyy::GetDatamemberType(var)))
        << "\n";
}

void handle_typedef(Cppyy::TCppScope_t var, std::ostringstream &file, int indent) {
    std::string indentation_str = indentation(indent);

    file << indentation_str
        << Cppyy::GetMethodName(var)
        << " = "
        << pythonize_type_name(Cppyy::GetFinalName(var))
        << "\n";
}

void handle_function(Cppyy::TCppScope_t fn, std::ostringstream &file, int indent) {
    static int A = 0;
    std::string indentation_str = indentation(indent);

    std::string func_name = Cppyy::GetFinalName(fn);
    std::string class_name;
    if (Cppyy::IsMethod(fn)) {
        class_name = Cppyy::GetMethodName(Cppyy::GetParentScope(fn));
        func_name = pythonize_method_name(func_name, class_name);
    }
    if (func_name.empty())
        return; // this is a overload that cannot be represented in python

    file << indentation_str
        << "@overload\n";
    if (Cppyy::IsStaticMethod(fn)) {
        file << indentation_str
            << "@staticmethod\n";
    }
    file << indentation_str
        << "def "
        << func_name
        << "(";
    bool prepend_comma = false;
    if (Cppyy::IsMethod(fn) && !Cppyy::IsStaticMethod(fn)) {
        // add the implicit self argument
        file << "self: "
            << pythonize_type_name(class_name);
        prepend_comma = true;
    }
    for (size_t i = 0, nArgs = Cppyy::GetMethodNumArgs(fn); i < nArgs; i++) {
        if (prepend_comma) {
            file << ", ";
            prepend_comma = false;
        }
        std::string arg_name = Cppyy::GetMethodArgName(fn, i);
        file << (arg_name.empty() ? ("_" + std::to_string(++A)) : arg_name)
            << ": "
            << pythonize_type_name(Cppyy::GetMethodArgTypeAsString(fn, i));
        std::string default_value = Cppyy::GetMethodArgDefault(fn, i); // FIXME: Pythonize this value
        if (!default_value.empty())
            file << " = " << default_value;
        if (i != nArgs - 1)
            file << ", ";
    }
    file << ") -> "
        << pythonize_type_name(Cppyy::GetMethodReturnTypeAsString(fn))
        << ": ...\n";
}

void handle_templates(Cppyy::TCppScope_t tmpl, std::ostringstream &file, int indent) { 
    static int A = 0;
    std::string indentation_str = indentation(indent);

    // TODO: handle parameter pack
    // TODO: handle defaults
    
    if (Cppyy::IsPureTemplatedMethod(tmpl)) {
        std::string func_name = Cppyy::GetFinalName(tmpl);
        std::string class_name;
        Cppyy::TCppScope_t parent = Cppyy::GetParentScope(tmpl);
        bool is_method = false;
        if (Cppyy::IsClass(parent)) {
            class_name = Cppyy::GetMethodName(parent);
            func_name = pythonize_method_name(func_name, class_name);
            is_method = true;
        }
        if (func_name.empty())
            return; // this is a overload that cannot be represented in python
        file << indentation_str
            << "@overload\n";
        if (Cppyy::IsStaticMethod(tmpl)) {
            file << indentation_str
                << "@staticmethod\n";
        }
        file << indentation_str
            << "def "
            << func_name
            << "[";
        // write template args
        for (size_t i = 0, nArgs = Cppyy::GetTemplateNumArgs(tmpl); i < nArgs; i++) {
            if (i != 0)
                file << ", ";
            std::string arg_name = Cppyy::GetTemplateArgName(tmpl, i);
            file << (arg_name.empty() ? ("_" + std::to_string(++A)) : arg_name);
        }
        file << "](";
        bool prepend_comma = false;
        if (is_method && !Cppyy::IsStaticMethod(tmpl)) {
            // add the implicit self argument
            file << "self: "
                << pythonize_type_name(class_name);
            prepend_comma = true;
        }
        for (size_t i = 0, nArgs = Cppyy::GetMethodNumArgs(tmpl); i < nArgs; i++) {
            if (prepend_comma) {
                file << ", ";
                prepend_comma = false;
            }
            std::string arg_name = Cppyy::GetMethodArgName(tmpl, i);
            file << (arg_name.empty() ? ("_" + std::to_string(++A)) : arg_name)
                << ": "
                << pythonize_type_name(Cppyy::GetMethodArgTypeAsString(tmpl, i));
            std::string default_value = Cppyy::GetMethodArgDefault(tmpl, i); // FIXME: Pythonize this value
            if (!default_value.empty())
                file << " = " << default_value;
            if (i != nArgs - 1)
                file << ", ";
        }
        file << ") -> "
            << pythonize_type_name(Cppyy::GetMethodReturnTypeAsString(tmpl))
            << ": ...\n";
    } else {
        // is templated class
    }
}

void handle_class(Cppyy::TCppScope_t cls, std::ostringstream &file, int indent) { 
    static size_t A = 0;
    std::string indentation_str = indentation(indent);

    bool is_templated = Cppyy::IsTemplateClass(cls);
    // TODO: handle inheritance
    file << indentation_str
        << "class "
        << Cppyy::GetMethodName(cls);
    if (is_templated) {
        file << "[";
        for (size_t i = 0, nArgs = Cppyy::GetTemplateNumArgs(cls); i < nArgs; i++) {
            if (i != 0)
                file << ", ";
            std::string arg_name = Cppyy::GetTemplateArgName(cls, i);
            file << (arg_name.empty() ? ("_" + std::to_string(++A)) : arg_name);
        }
        file << "]";
    }
    file << ":\n";
    
    std::vector<Cppyy::TCppScope_t> members;
    Cppyy::GetDatamembers(cls, members);
    for (Cppyy::TCppScope_t datamember: members)
        handle_variable(datamember, file, indent + 1);
    
    members.clear();
    Cppyy::GetClassMethods(cls, members);
    file << "\n";
    for (Cppyy::TCppScope_t datamember: members) {
        file << "\n";
        handle_function(datamember, file, indent + 1);
    }
    
    members.clear();
    Cppyy::GetTemplatedMethods(cls, members);
    file << "\n";
    for (Cppyy::TCppScope_t datamember: members) {
        file << "\n";
        handle_templates(datamember, file, indent + 1);
    }
    file << "\n\n";

    // FIXME: typedefs, enums, & nexted classes missing
}

void handle_namespace(Cppyy::TCppScope_t ns, std::ostringstream &file, int indent) {
    std::vector<Cppyy::TCppScope_t> members;
    Cppyy::GetMemberInNamespace(ns, members);
    file << "class " << Cppyy::GetMethodName(ns) << ":\n";
    for (Cppyy::TCppScope_t s: members)
        handle_scope(s, file, indent + 1);
}

void handle_scope(Cppyy::TCppScope_t typ, std::ostringstream &file, int indent) {
    if (Cppyy::IsNamespace(typ)) {
        handle_namespace(typ, file, indent);
    }
    if (Cppyy::IsClass(typ) || Cppyy::IsTemplateClass(typ)) {
        handle_class(typ, file, indent);
    }
    if (Cppyy::IsTypedefed(typ)) {
        handle_typedef(typ, file, indent);
    }
    if (Cppyy::IsFunction(typ) && !Cppyy::IsMethod(typ) && !Cppyy::IsPureNamespace(Cppyy::GetParentScope(typ))) {
        handle_function(typ, file, indent);
    }
    if (Cppyy::IsVariable(typ) && !Cppyy::IsPureNamespace(Cppyy::GetParentScope(typ)) && !Cppyy::IsClass(Cppyy::GetParentScope(typ))) {
        handle_variable(typ, file, indent);
    }
}

PyObject *generate_typehints(PyObject *, PyObject *args) {
    const char *type_name = nullptr;
    const char *file_name = nullptr;
    if (!PyArg_ParseTuple(args, "s|s", &type_name, &file_name))
        return nullptr;
    Cppyy::TCppScope_t typ = Cppyy::GetScope(type_name);
    if (!typ)
        typ = Cppyy::GetNamed(type_name);
    if (!typ)
        return PyErr_Format(PyExc_TypeError, "Unknown Type. Try Parent.");

    std::ostringstream code;
    handle_scope(typ, code);
    code << "\n";

    std::string typehint = code.str();
    if (typehint.empty())
        return PyErr_Format(PyExc_TypeError, "Cannot generate typehints for the given type <%s>. Try Parent.", Cppyy::GetScopedFinalName(typ).c_str());

    if (!file_name)
        return PyUnicode_FromString(typehint.c_str());

    std::fstream file(file_name, std::ios::out | std::ios::ate); // append
    file << typehint;
    Py_RETURN_NONE;
}
