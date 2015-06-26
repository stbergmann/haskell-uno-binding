/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "writer.hxx"

#include <string>
#include <vector>
#include <fstream>
#include <ostream>
#include <iostream>

#include "osl/file.hxx"
#include "osl/process.h"
#include "rtl/ref.hxx"
#include "rtl/ustrbuf.hxx"

#include "file.hxx"
#include "types.hxx"

using rtl::OUString;
using rtl::OUStringBuffer;

// temp
extern OUString openModulesFor(std::vector<OUString> & modules, OUString const & name);

// local constants
const OUString cxxFileExtension (".cpp");
const OUString hxxFileExtension (".hpp");
const OUString hsFileExtension (".hs");
const OUString functionPrefix ("hsuno_");
const OUString getterPrefix ("Get");
const OUString setterPrefix ("Set");
const OUString exceptionName ("e");
const OUString headerGuardPrefix ("HSUNO_");
const OUString headerGuardSuffix ("_H");
const OUString headerFileExtension (".hpp");
const OUString hsModulePrefix ("LibreOffice.");

// LOCAL FUNCTIONS
// interface functions
void writeInterfaceAux (std::ostream & cxx, std::ostream & hxx, std::ostream & hs,
        Entity const & entity);
void writeInterfaceHsForeignImports (std::ostream & hs,
        rtl::OUString const & entityName, rtl::Reference<unoidl::InterfaceTypeEntity> entity);
void writeInterfaceHsMethods (std::ostream & hs,
        rtl::OUString const & entityName, rtl::Reference<unoidl::InterfaceTypeEntity> entity);
// exception functions
static void writeExceptionAux (std::ostream & cxx, std::ostream & hxx, std::ostream & hs,
        rtl::OUString const & name, rtl::Reference<unoidl::ExceptionTypeEntity> entity);
void writeExceptionCxxGetter(std::ostream & cxx, OUString const & entityName,
        OUString const & memberType, OUString const & memberName);
void writeExceptionHxxGetter(std::ostream & hxx, OUString const & entityName,
        OUString const & memberType, OUString const & memberName);
void writeFunctionDeclaration(std::ostream & out,
        OUString const & module,
        OUString const & functionName,
        OUString const & returnType,
        std::vector< OUString > const & parameters);
void writeExceptionHsGetter(std::ostream & hs, OUString const & entityName,
        OUString const & memberType, OUString const & memberName);
// interface-based singleton functions
void writeInterfaceBasedSingletonAux (std::ostream & cxx, std::ostream & hxx,
        std::ostream & hs, Entity const & entity);
// service-based singleton functions
void writeServiceBasedSingletonAux (std::ostream & cxx, std::ostream & hxx,
        std::ostream & hs, Entity const & entity);
// auxiliary functions
OUString entityHeaderGuardName (Entity const & entity);
OUString entityToHeaderGuardName (OUString const & entity);
OUString entityToHeaderFileName (OUString const & entity);
OUString entityToFileName (OUString const & entity);
OUString entityToInclude (OUString const & entity);
void indent(std::ostream & out, int n);

void writeInterface (Entity const & entity)
{
    OUString name (entity.name);
    OUString entityFullName (entity.module.getName() + "." + name);
    const OUString cxxFileName(name + cxxFileExtension);
    const OUString hxxFileName(name + hxxFileExtension);
    const OUString hsFileName(name + hsFileExtension);

    File cxx ("gen/", entity.module.asPathCapitalized(), cxxFileName);
    File hxx ("gen/", entity.module.asPathCapitalized(), hxxFileName);
    File hs ("gen/", entity.module.asPathCapitalized(), hsFileName);

    writeInterfaceAux(cxx, hxx, hs, entity);
}

void writeInterfaceAux (std::ostream & cxx, std::ostream & hxx, std::ostream & hs,
        Entity const & entity)
{
    // entity module (including its name)
    Module eModule = entity.module.createSubModule(entity.name);
    // entity fully qualified name
    OUString eFQN (eModule.asNamespace());

    rtl::Reference<unoidl::InterfaceTypeEntity> ent (
            static_cast<unoidl::InterfaceTypeEntity *>(entity.entity.get()));
    OUString entityName (entity.name);
    OUString entityFullName (eModule.getName());

    // cxx
    cxx << "#include \"" << entityName << headerFileExtension << "\"" << std::endl;
    cxx << "#include \"UNO/Binary.hxx\"" << std::endl;
    cxx << "#include \"rtl/ref.hxx\"" << std::endl;
    cxx << std::endl;

    // hxx
    const OUString headerGuardName (entityHeaderGuardName(entity));
    hxx << "#ifndef " << headerGuardName << std::endl;
    hxx << "#define " << headerGuardName << std::endl;
    hxx << std::endl;
    hxx << "#include \"rtl/ustring.hxx\"" << std::endl;
    hxx << "#include \"uno/any2.hxx\"" << std::endl;
    hxx << "#include \"uno/mapping.hxx\"" << std::endl;
    hxx << std::endl;
    for (std::vector<unoidl::InterfaceTypeEntity::Method>::const_iterator
            j(ent->getDirectMethods().begin());
            j != ent->getDirectMethods().end(); ++j)
    {
        OUString returnType (toCppType(j->returnType));
        if (isStringType(j->returnType))
            returnType += " *";
        std::vector<OUString> parameters;
        parameters.push_back("void * rIface");
        parameters.push_back("uno_Any ** exception");
        for (std::vector<unoidl::InterfaceTypeEntity::Method::Parameter>::const_iterator
                k(j->parameters.begin()) ;
                k != j->parameters.end() ; ++k) {
            if (isStringType(k->type))
                parameters.push_back(toCppType(k->type) + " * " + k->name);
            else
                parameters.push_back(toCppType(k->type) + " " + k->name);
        }
        writeFunctionDeclaration(hxx, entityFullName, j->name, returnType, parameters);
        hxx << ";" << std::endl;

        // cxx
        writeFunctionDeclaration(cxx, entityFullName, j->name, returnType, parameters);
        cxx << std::endl;
        cxx << "{" << std::endl;
        // prepare interface
        indent(cxx, 4);
        cxx << "rtl::Reference< " << eFQN << " > * rIface2 ="
            << "static_cast< rtl::Reference< " << eFQN << " > * >(rIface);"
            << std::endl;
        cxx << std::endl;
        indent(cxx, 4);
        cxx << "uno_Interface * iface = static_cast< uno_Interface * >(g_cpp2uno.mapInterface("
            << "rIface2->get(), "
            << "cppu::UnoType< " << entityFullName << " >::get()"
            << "))" << std::endl;
        // result type
        indent(cxx, 4);
        if (isStringType(j->returnType)) {
            cxx << "rtl_uString * result = 0;" << std::endl;
        } else if (isPrimitiveType(j->returnType)) {
            cxx << toCppType(j->returnType) << " result;" << std::endl;
        } else {
            // TODO non-primitive types
        }
        // prepare arguments
        indent(cxx, 4);
        cxx << "void * args [" << j->parameters.size() << "];" << std::endl;
        int argIdx = 0;
        for (std::vector<unoidl::InterfaceTypeEntity::Method::Parameter>::const_iterator
                k(j->parameters.begin()) ;
                k != j->parameters.end() ; ++k) {
            indent(cxx, 4);
            cxx << "args[" << argIdx << "] = ";
            if (isStringType(k->type)) {
                cxx << "const_cast<rtl_uString **>(&" << k->name << "->pData)" << std::endl;
            } else if (isPrimitiveType(k->type)) {
                cxx << "&" << j->name << std::endl;
            } else {
                // TODO non-primitive types
            }
            ++argIdx;
        }
        // execute the call
        indent(cxx, 4);
        cxx << "makeBinaryUnoCall(iface, \""
            << entityFullName << "::" << j->name
            << "\", &result, args, exception);" << std::endl;
        // create result and return
        indent(cxx, 4);
        if (isStringType(j->returnType)) {
            cxx << "return new rtl::OUString(result, SAL_NO_ACQUIRE);";
        } else if (isPrimitiveType(j->returnType)) {
            cxx << "return result;";
        } else {
            // TODO non-primitive types
        }
        cxx << std::endl;
        cxx << "}" << std::endl;
    }
    hxx << std::endl;
    hxx << "#endif // " << headerGuardName << std::endl;

    // hs
    // TODO create correct module name
    hs << "module " << hsModulePrefix << "TODO" << " where" << std::endl;
    hs << std::endl;
    hs << "import SAL.Types" << std::endl;
    hs << "import UNO.Binary" << std::endl;
    hs << "import UNO.Service" << std::endl;
    hs << "import Text" << std::endl;
    hs << std::endl;
    hs << "import qualified Control.Exception E" << std::endl;
    hs << "import Control.Monad" << std::endl;
    hs << "import Data.Text (Text)" << std::endl;
    hs << "import Foreign" << std::endl;
    hs << std::endl;

    writeInterfaceHsForeignImports (hs, entityName, ent);
    hs << std::endl;

    hs << "class Service a => " << entityName << " a where" << std::endl;
    writeInterfaceHsMethods (hs, entityName, ent);
}

void writeInterfaceHsForeignImports (std::ostream & hs,
        rtl::OUString const & entityName, rtl::Reference<unoidl::InterfaceTypeEntity> entity)
{
    for (std::vector<unoidl::InterfaceTypeEntity::Method>::const_iterator
            j(entity->getDirectMethods().begin());
            j != entity->getDirectMethods().end(); ++j)
    {
        OUString cMethodName (j->name);
        hs << "foreign import ccall \"" << cMethodName << "\" c" << j->name << std::endl;
        // method type
        indent(hs, 4);
        hs << " :: Ptr UnoInterface -> Ptr (Ptr Any) -> "; 
        for (std::vector<unoidl::InterfaceTypeEntity::Method::Parameter>::const_iterator
                k(j->parameters.begin());
                k != j->parameters.end(); ++k)
        {
            if (isPrimitiveType(j->returnType) && !isStringType(j->returnType)) {
                hs << toHsCppType(j->returnType);
            } else {
                hs << "Ptr " + toHsCppType(j->returnType);
            }
            hs << " -> ";
        }
        hs << "IO " ;
        if (j->returnType == "void") {
            hs << "()";
        } else if (isPrimitiveType(j->returnType) && !isStringType(j->returnType)) {
            hs << toHsCppType(j->returnType);
        } else {
            hs << "(Ptr " + toHsCppType(j->returnType) + ")";
        }
        hs << std::endl;
    }
}

void writeInterfaceHsMethods (std::ostream & hs,
        rtl::OUString const & entityName, rtl::Reference<unoidl::InterfaceTypeEntity> entity)
{
    for (std::vector<unoidl::InterfaceTypeEntity::Method>::const_iterator
            j(entity->getDirectMethods().begin());
            j != entity->getDirectMethods().end(); ++j)
    {
        // method type
        indent(hs, 4);
        hs << j->name << " :: a -> "; 
        for (std::vector<unoidl::InterfaceTypeEntity::Method::Parameter>::const_iterator
                k(j->parameters.begin());
                k != j->parameters.end(); ++k)
        {
            hs << toHsType(k->type) << " -> ";
        }
        hs << "IO " << toHsType(j->returnType) << std::endl;
        // method arguments
        indent(hs, 4);
        hs << j->name << " a "; 
        for (std::vector<unoidl::InterfaceTypeEntity::Method::Parameter>::const_iterator
                k(j->parameters.begin());
                k != j->parameters.end(); ++k)
        {
            hs << toHsType(k->name) << " ";
        }
        hs << "= do" << std::endl;
        // get correct interface
        indent(hs, 8);
        hs << "let iface = getInterface a" << std::endl;
        // prepare arguments
        std::vector< OUString > arguments;
        for (std::vector<unoidl::InterfaceTypeEntity::Method::Parameter>::const_iterator
                k(j->parameters.begin());
                k != j->parameters.end(); ++k)
        {
            indent(hs, 8);
            if (isStringType(k->type)) {
                OUString s (hsTypeCxxPrefix(k->type) + k->name);
                arguments.push_back(s);
                hs << s << " <- hs_text_to_oustring " << k->name << std::endl;
            } else if (isPrimitiveType(k->type)) {
                arguments.push_back(k->type);
            }
        }
        // prepare exception pointer
        indent(hs, 8);
        hs << "with nullPtr $ \\ exceptionPtr -> do" << std::endl;
        // run method
        indent(hs, 12);
        hs << "result <- c" << j->name << " iface";
        for (std::vector< OUString >::const_iterator
                k(arguments.begin()); k != arguments.end(); ++k)
        {
            hs << " " << *k;
        }
        hs << std::endl;
        // check for exceptions
        indent(hs, 12);
        hs << "aException <- peek exceptionPtr" << std::endl;
        indent(hs, 12);
        hs << "when (aException /= nullPtr) (error \"exceptions not yet implemented\")" << std::endl;
        // prepare result
        OUString methodResult;
        if (j->returnType == "void") {
            methodResult = "()";
        } else {
            methodResult = "methodResult";
            if (isStringType(j->returnType)) {
                indent(hs, 12);
                hs << "methodResult <- hs_oustring_to_text result" << std::endl;
                indent(hs, 12);
                hs << "c_delete_oustring result" << std::endl;
            } else if (isPrimitiveType(j->returnType)) {
                indent(hs, 12);
                hs << "let methodResult = result" << std::endl;
            } else {
                indent(hs, 12);
                hs << "error \"non-primitive types are not yet supported\"";
                methodResult = "undefined";
            }
        }
        // return
        indent(hs, 12);
        hs << "return " << methodResult << std::endl;
    }
}

void writeException (Entity const & entity)
{
    OUString name (entity.name);
    OUString entityFullName (entity.module.getName() + "." + name);
    const OUString cxxFileName(name + cxxFileExtension);
    const OUString hxxFileName(name + hxxFileExtension);
    const OUString hsFileName(name + hsFileExtension);

    File cxx ("gen/", entity.module.asPathCapitalized(), cxxFileName);
    File hxx ("gen/", entity.module.asPathCapitalized(), hxxFileName);
    File hs ("gen/", entity.module.asPathCapitalized(), hsFileName);

    rtl::Reference<unoidl::ExceptionTypeEntity> ent2(
            static_cast<unoidl::ExceptionTypeEntity *>(entity.entity.get()));
    writeExceptionAux(cxx, hxx, hs, entityFullName, ent2);
}

static void writeExceptionAux (std::ostream & cxx, std::ostream & hxx, std::ostream & hs,
        rtl::OUString const & name, rtl::Reference<unoidl::ExceptionTypeEntity> entity)
{
    const OUString headerFileName = entityToHeaderFileName(name);
    const OUString headerGuardName = entityToHeaderGuardName(name);

    // cxx
    cxx << "#include \"" << headerFileName << "\"" << std::endl;
    cxx << std::endl;

    // hxx
    hxx << "#ifndef " << headerGuardPrefix << headerGuardName << headerGuardSuffix << std::endl;
    hxx << "#define " << headerGuardPrefix << headerGuardName << headerGuardSuffix << std::endl;
    hxx << std::endl;
    hxx << "#include \"" << entityToInclude(name) << "\"" << std::endl;
    hxx << std::endl;

    for (std::vector<unoidl::ExceptionTypeEntity::Member>::const_iterator
            j(entity->getDirectMembers().begin());
            j != entity->getDirectMembers().end(); ++j)
    {
        if (isPrimitiveType(j->type)) {
            // cxx
            writeExceptionCxxGetter(cxx, name, j->type, j->name);
            // hxx
            writeExceptionHxxGetter(hxx, name, j->type, j->name);
            // hs
            writeExceptionHsGetter(hs, name, j->type, j->name);
        } else {
            // TODO support for non-primitive types
        }
    }

    // hxx
    hxx << std::endl;
    hxx << "#endif // " << headerGuardPrefix << headerGuardName << headerGuardSuffix << std::endl;
}

void writeExceptionCxxGetter(std::ostream & cxx, OUString const & entityName,
        OUString const & memberType, OUString const & memberName)
{
    OUString cxxRetType (toCppType(memberType));
    if (isStringType(memberType))
        cxxRetType += " *";
    cxx << "extern \"C\"" << std::endl;
    cxx << cxxRetType << ' '
        << functionPrefix << toFunctionPrefix(entityName) << getterPrefix << memberName
        << "(" << toCppType(entityName) << " * " << exceptionName << ") "
        << "{" << std::endl;
    cxx << "    return ";
    if (isStringType(memberType))
        cxx << "&" ;
    cxx << exceptionName << "->" << memberName << ";"
        << std::endl;
    cxx << "}" << std::endl;
}

void writeExceptionHxxGetter(std::ostream & hxx, OUString const & entityName,
        OUString const & memberType, OUString const & memberName)
{
    OUString cxxRetType (toCppType(memberType));
    if (isStringType(memberType))
        cxxRetType += " *";
    hxx << "extern \"C\"" << std::endl;
    hxx << cxxRetType << ' '
        << functionPrefix << toFunctionPrefix(entityName) << getterPrefix << memberName
        << "(" << toCppType(entityName) << " * " << exceptionName << ");" << std::endl;
}

void writeFunctionDeclaration(std::ostream & out,
        OUString const & module,
        OUString const & functionName,
        OUString const & returnType,
        std::vector< OUString > const & parameters)
{
    out << "extern \"C\"" << std::endl;
    out << returnType << ' '
        << functionPrefix << toFunctionPrefix(module) << "_" << functionName;
    out << "(";
    for (std::vector< OUString >::const_iterator it (parameters.begin()) ;
            it != parameters.end() ; ++it) {
        if (it != parameters.begin())
            out << ", ";
        out << * it;
    }
    out << ")";
}

void writeExceptionHsGetter(std::ostream & hxx, OUString const & entityName,
        OUString const & memberType, OUString const & memberName)
{
    hxx << "TODO" << std::endl;
}

void writeInterfaceBasedSingleton (Entity const & entity)
{
    OUString name (entity.name);
    OUString entityFullName (entity.module.getName() + "." + name);
    const OUString cxxFileName(name + cxxFileExtension);
    const OUString hxxFileName(name + hxxFileExtension);
    const OUString hsFileName(name + hsFileExtension);

    File cxx ("gen/", entity.module.asPathCapitalized(), cxxFileName);
    File hxx ("gen/", entity.module.asPathCapitalized(), hxxFileName);
    File hs ("gen/", entity.module.asPathCapitalized(), hsFileName);

    writeInterfaceBasedSingletonAux(cxx, hxx, hs, entity);
}

void writeInterfaceBasedSingletonAux (std::ostream & cxx, std::ostream & hxx,
        std::ostream & hs, Entity const & entity)
{
    rtl::Reference<unoidl::InterfaceBasedSingletonEntity> ent (
            static_cast<unoidl::InterfaceBasedSingletonEntity *>(entity.entity.get()));
    OUString entityName (entity.name);
    OUString entityFullName (entity.module.getName() + "." + entityName);
    // entity module (including its name)
    Module eModule = entity.module.createSubModule(entity.name);
    // entity fully qualified name
    OUString eFQN (eModule.asNamespace());
    // entity base module
    Module eBaseModule(ent->getBase());
    // entity base fully qualified name
    OUString eBaseFQN(eBaseModule.asNamespace());

    // cxx
    cxx << "#include \"" << entityName << headerFileExtension << "\"" << std::endl;
    cxx << "#include \"" << "UNO/Binary.hxx" << "\"" << std::endl;
    cxx << "#include \"" << entity.module.asPath() << "/" << entityName
        << ".hpp\"" << std::endl;
    cxx << std::endl;
    writeFunctionDeclaration(cxx, eModule.getName(), "new", "void *", std::vector< OUString >());
    cxx << " {" << std::endl;
    indent(cxx, 4);
    cxx << "css::uno::Reference< " << eBaseFQN << " > g_expander = "
        << eFQN << "::get(g_context);" << std::endl;

    cxx << "}" << std::endl;

    // hxx
    const OUString headerGuardName (entityHeaderGuardName(entity));
    hxx << "#ifndef " << headerGuardName << std::endl;
    hxx << "#define " << headerGuardName << std::endl;
    hxx << std::endl;
    writeFunctionDeclaration(hxx, eModule.getName(), "new", "void *", std::vector< OUString >());
    hxx << ";" << std::endl;
    hxx << std::endl;
    hxx << "#endif // " << headerGuardName << std::endl;

    // hs
    hs << "module " << eModule.getNameCapitalized() << " where" << std::endl;
    hs << std::endl;
    hs << "import " << eBaseModule.getNameCapitalized() << std::endl;
    hs << "import UNO.Binary" << std::endl;
    hs << "import UNO.Service" << std::endl;
    hs << std::endl;
    hs << "import Control.Applicative ((<$>))" << std::endl;
    hs << "import Foreign.Ptr" << std::endl;
    hs << std::endl;
    hs << "data " << capitalize(entity.name) << " = " << capitalize(entity.name)
       << " (Ptr UnoInterface)" << std::endl; // FIXME content should not be a UnoInterface
    hs << std::endl;
    hs << "instance Service " << capitalize(entity.name) << " where"
       << std::endl;
    hs << "    getInterface (" << capitalize(entity.name) << " ptr) = ptr"
       << std::endl;
    hs << std::endl;
    hs << "instance " << capitalize(eBaseModule.getLastName()) << " "
       << capitalize(entity.name) << " where" << std::endl;
    hs << std::endl;
    hs << entity.name << "New :: IO " << capitalize(entity.name) << std::endl;
    hs << entity.name << "New = " << capitalize(entity.name) << " <$> c"
       << capitalize(entity.name) << "_new" << std::endl;
    hs << std::endl;
    hs << "foreign import ccall \"" << functionPrefix
       << toFunctionPrefix(entityFullName) << "_new" << "\" c"
       << capitalize(entity.name) << "_new" << std::endl;
    hs << "    :: IO (Ptr UnoInterface)" << std::endl; // FIXME content should not be a UnoInterface
    hs << std::endl;
}

void writeServiceBasedSingleton (Entity const & entity)
{
    OUString name (entity.name);
    OUString entityFullName (entity.module.getName() + "." + name);
    const OUString cxxFileName(name + cxxFileExtension);
    const OUString hxxFileName(name + hxxFileExtension);
    const OUString hsFileName(name + hsFileExtension);

    File cxx ("gen/", entity.module.asPathCapitalized(), cxxFileName);
    File hxx ("gen/", entity.module.asPathCapitalized(), hxxFileName);
    File hs ("gen/", entity.module.asPathCapitalized(), hsFileName);

    writeServiceBasedSingletonAux(cxx, hxx, hs, entity);
}

void writeServiceBasedSingletonAux (std::ostream & cxx, std::ostream & hxx,
        std::ostream & hs, Entity const & entity)
{
    rtl::Reference<unoidl::ServiceBasedSingletonEntity> ent (
            static_cast<unoidl::ServiceBasedSingletonEntity *>(entity.entity.get()));
    OUString entityName (entity.name);
    OUString entityFullName (entity.module.getName() + "." + entityName);

    std::cout << "TODO" << std::endl;
    // TODO
}

OUString entityHeaderGuardName (Entity const & entity)
{
    OUStringBuffer buf;
    buf.append(headerGuardPrefix);
    buf.append(entity.module.createSubModule(entity.name).asHeaderGuard());
    buf.append(headerGuardSuffix);
    return buf.makeStringAndClear();
}

OUString entityToHeaderGuardName (OUString const & entity)
{
    return entity.replace('.', '_').toAsciiUpperCase();
}

OUString entityToHeaderFileName (OUString const & entity)
{
    // FIXME only return the name and the file, dropping its parent directory
    return entity.replace('.', '/') + headerFileExtension;
}

OUString entityToFileName (OUString const & entity)
{
    return entity.replace('.', '/');
}

OUString entityToInclude (OUString const & entity)
{
    return entity.replace('.', '/') + headerFileExtension;
}

void indent (std::ostream & out, int n)
{
    while (n-- > 0)
        out << ' ';
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
