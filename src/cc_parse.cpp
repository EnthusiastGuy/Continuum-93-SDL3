// C parser and types: CParser.cs, CTypes.cs (see cc_internal.h). Builds the fully typed AST: identifiers are
// resolved, implicit conversions become Cast nodes, pointer arithmetic is scaled, ++/--/op=/initializers lowered.
#include <algorithm>
#include <tuple>

#include "cc_internal.h"

namespace c93::cc {

// ---------------------------------------------------------------------------- types
void outOfRange() { fatal("ArgumentOutOfRangeException"); }

Type tVoid{.kind = TyK::Void}, tBool{.kind = TyK::Char, .uns = true, .isBool = true}, tChar{.kind = TyK::Char},
     tUChar{.kind = TyK::Char, .uns = true}, tShort{.kind = TyK::Short}, tUShort{.kind = TyK::Short, .uns = true},
     tInt{.kind = TyK::Int}, tUInt{.kind = TyK::Int, .uns = true};

bool Type::isComplete() const {
    switch (kind) {
        case TyK::Void: return false;
        case TyK::Array: return length >= 0 && base->isComplete();
        case TyK::Struct: return st->complete;
        case TyK::Function: return false;
        default: return true;
    }
}

int Type::size() const {
    switch (kind) {
        case TyK::Void: return 1;   // GNU-style: arithmetic on void* uses size 1
        case TyK::Char: return 1;
        case TyK::Short: return 2;
        case TyK::Array: return length < 0 ? 0 : length * base->size();
        case TyK::Struct: return st->size;
        case TyK::Function: return 1;
        default: return 4;
    }
}

std::string Type::str() const {
    switch (kind) {
        case TyK::Void: return "void";
        case TyK::Char: return isBool ? "_Bool" : uns ? "unsigned char" : "char";
        case TyK::Short: return uns ? "unsigned short" : "short";
        case TyK::Int: return isEnum ? "enum" : uns ? "unsigned int" : "int";
        case TyK::Pointer: return base->str() + "*";
        case TyK::Array: return base->str() + "[" + (length >= 0 ? std::to_string(length) : "") + "]";
        case TyK::Struct: return (st->isUnion ? "union " : "struct ") + (st->hasTag ? st->tag : "<anonymous>");
        case TyK::Function: {
            std::string ps;
            if (hasProto) {
                if (params.empty() && !variadic) ps = "void";
                else for (size_t i = 0; i < params.size(); ++i) ps += (i ? ", " : "") + params[i].type->str();
            }
            if (variadic) ps += ", ...";
            return base->str() + "(" + ps + ")";
        }
    }
    return "?";
}

bool sameType(const Type* a, const Type* b) {
    if (a == b) return true;
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case TyK::Void: return true;
        case TyK::Char: case TyK::Short: case TyK::Int: return a->uns == b->uns && a->isBool == b->isBool;
        case TyK::Pointer: return sameType(a->base, b->base);
        case TyK::Array: return sameType(a->base, b->base) && (a->length < 0 || b->length < 0 || a->length == b->length);
        case TyK::Struct: return a->st == b->st;
        case TyK::Function:
            if (!sameType(a->base, b->base)) return false;
            if (!a->hasProto || !b->hasProto) return true;
            if (a->variadic != b->variadic || a->params.size() != b->params.size()) return false;
            for (size_t i = 0; i < a->params.size(); ++i) if (!sameType(a->params[i].type, b->params[i].type)) return false;
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------- parser
namespace {

const std::unordered_set<std::string> kTypeKeywords = {
    "void", "_Bool", "char", "short", "int", "long", "signed", "unsigned", "struct", "union", "enum",
    "typedef", "static", "extern", "inline", "const", "volatile", "auto", "register", "restrict",
    "__restrict", "float", "double", "_Noreturn", "__inline"};
const std::unordered_set<std::string> kKeywords = {
    "return", "if", "else", "while", "do", "for", "switch", "case", "default", "break", "continue", "goto",
    "sizeof", "asm", "__asm", "__asm__"};
bool isKeyword(const std::string& s) { return kKeywords.count(s) || kTypeKeywords.count(s); }

struct DeclSpec { Type* type = nullptr; bool isTypedef = false, isStatic = false, isExtern = false; };
struct Init { const Token* tok; bool isList = false; Node* expr = nullptr; std::vector<Init> items; };
using Leaves = std::vector<std::tuple<int, Type*, Node*>>;

int64_t normalize(int64_t v, const Type* t) {
    if (t->isBool) return v != 0 ? 1 : 0;
    switch (t->size()) {
        case 1: return t->uns ? int64_t(uint8_t(v)) : int64_t(int8_t(v));
        case 2: return t->uns ? int64_t(uint16_t(v)) : int64_t(int16_t(v));
        default: return t->uns || t->isPointer() ? int64_t(uint32_t(v)) : int64_t(int32_t(v));
    }
}

} // namespace

class Parser {
public:
    Parser(Arena& a, Unit& unit, Errors& errors) : a_(a), unit_(unit), errors_(errors) {}
    int labelCounter = 0;
    std::unordered_map<std::string, Symbol*> globals;

    void parseUnit(const TokenList& toks, bool libraryMode) {
        toks_ = &toks;
        pos_ = 0;
        library_ = libraryMode;
        while (!atEof()) {
            if (accept(";")) continue;
            DeclSpec spec = declSpec(true);
            if (spec.isTypedef) { typedefs(spec); continue; }
            if (accept(";")) continue;   // e.g. "struct S { ... };"
            const Token* name;
            Type* ty = declarator(spec.type, name);
            if (!name) fail(cur(), "Expected an identifier in declaration");
            if (ty->isFunction() && peek("{")) { functionDefinition(ty, name, spec); continue; }
            while (true) {   // declarations, possibly several separated by commas
                if (ty->isFunction()) declareFunction(ty, *name, spec, false);
                else globalVariable(ty, *name, spec);
                if (accept(";")) break;
                expect(",");
                ty = declarator(spec.type, name);
                if (!name) fail(cur(), "Expected an identifier in declaration");
            }
        }
    }

private:
    Arena& a_;
    Unit& unit_;
    Errors& errors_;
    const TokenList* toks_ = nullptr;
    size_t pos_ = 0;
    std::vector<std::unordered_map<std::string, Symbol*>> scopes_{1};
    std::vector<std::unordered_map<std::string, Type*>> tagScopes_{1};
    Function* fn_ = nullptr;
    int brk_ = -1, cont_ = -1;   // current break/continue target (-1: none)
    Node* switch_ = nullptr;
    std::unordered_map<std::string, int> userLabels_;
    std::vector<Node*> gotos_;
    int staticCounter_ = 0;
    bool library_ = false;

    // ------------------------------------------------------------------ tokens and errors
    const Token& cur() const {
        if (pos_ >= toks_->size()) outOfRange();
        return *(*toks_)[pos_];
    }
    const Token* curp() const { return &cur(); }
    const Token& peek2() const { return pos_ + 1 < toks_->size() ? *(*toks_)[pos_ + 1] : *toks_->back(); }
    bool atEof() const { return cur().kind == TK::Eof; }
    bool peek(std::string_view s) const { return cur().is(s) && cur().kind != TK::String; }
    bool accept(std::string_view s) {
        const Token& c = cur();
        if ((c.kind == TK::Punct || c.kind == TK::Identifier) && c.text == s) { ++pos_; return true; }
        return false;
    }
    const Token* expect(std::string_view s) {
        if (!accept(s)) fail(cur(), "Expected '" + std::string(s) + "' but found '" + cur().str() + "'");
        return (*toks_)[pos_ - 1];
    }
    const Token* expectIdentifier() {
        if (cur().kind != TK::Identifier || isKeyword(cur().text)) fail(cur(), "Expected an identifier but found '" + cur().str() + "'");
        return (*toks_)[pos_++];
    }
    int newLabel() { return labelCounter++; }   // ".c__L<n>"
    [[noreturn]] void fail(const Token& at, std::string message) {
        errors_.push_back({*at.file, at.line, at.col, std::move(message)});
        throw Abort{};
    }

    Type* pointerTo(Type* t) { Type p{TyK::Pointer}; p.base = t; p.uns = true; return a_.type(p); }
    Type* arrayOf(Type* t, int n) { Type p{TyK::Array}; p.base = t; p.length = n; return a_.type(p); }
    Type* decay(Type* t) { return t->kind == TyK::Array ? pointerTo(t->base) : t->kind == TyK::Function ? pointerTo(t) : t; }

    // ------------------------------------------------------------------ top level
    void typedefs(const DeclSpec& spec) {
        bool first = true;
        while (!accept(";")) {
            if (!first) expect(",");
            first = false;
            const Token* name;
            Type* ty = declarator(spec.type, name);
            if (!name) fail(cur(), "Typedef requires a name");
            Symbol* s = &a_.symbols.emplace_back();
            s->name = name->text; s->kind = SymK::Typedef; s->type = ty;
            declareInScope(*name, s);
        }
    }

    Symbol* declareFunction(Type* ty, const Token& nameTok, const DeclSpec& spec, bool isDefinition) {
        const std::string& name = nameTok.text;
        if (ty->base->isStruct()) fail(nameTok, "Functions returning structs by value are not supported; return a pointer instead");
        if (ty->base->isArray()) fail(nameTok, "Functions cannot return arrays");
        if (auto it = globals.find(name); it != globals.end()) {
            Symbol* e = it->second;
            if (e->kind != SymK::Function) fail(nameTok, "'" + name + "' redeclared as a different kind of symbol");
            if (!sameType(e->type, ty)) fail(nameTok, "Conflicting types for '" + name + "': " + e->type->str() + " vs " + ty->str());
            if (isDefinition && e->isDefined) fail(nameTok, "Redefinition of function '" + name + "'");
            if (ty->hasProto && !e->type->hasProto) e->type = ty;
            return e;
        }
        Symbol* s = &a_.symbols.emplace_back();
        s->name = name; s->kind = SymK::Function; s->type = ty; s->label = ".c_" + name; s->isStatic = spec.isStatic;
        globals[name] = s;
        scopes_[0][name] = s;
        return s;
    }

    void functionDefinition(Type* ty, const Token* nameTok, const DeclSpec& spec) {
        if (library_) {
            auto it = globals.find(nameTok->text);
            if (it != globals.end() && it->second->isDefined) { skipBalancedBraces(); return; }   // the user's definition wins
        }
        if (ty->variadic) fail(*nameTok, "Defining variadic functions is not supported");
        Symbol* sym = declareFunction(ty, *nameTok, spec, true);
        sym->isDefined = true;
        sym->type = ty;
        Function* fn = &a_.functions.emplace_back();
        fn->name = nameTok->text; fn->sym = sym; fn->type = ty; fn->returnLabel = ".c_" + nameTok->text + "__ret";
        sym->fn = fn;
        fn_ = fn;
        userLabels_.clear();
        gotos_.clear();
        enterScope();
        if (ty->hasProto) {
            for (const Param& p : ty->params) {
                if (!p.hasName) fail(*nameTok, "Parameter name omitted in definition of '" + nameTok->text + "'");
                if (p.type->isStruct()) fail(*nameTok, "Passing structs by value is not supported; pass a pointer instead");
                Symbol* ps = &a_.symbols.emplace_back();
                ps->name = p.name; ps->kind = SymK::Local; ps->type = p.type;
                fn->params.push_back(ps);
                scopes_.back()[p.name] = ps;
            }
        }
        fn->body = compound(false);
        leaveScope();
        for (Node* g : gotos_) {   // resolve gotos
            auto it = userLabels_.find(g->text);
            if (it == userLabels_.end()) fail(*g->tok, "Label '" + g->text + "' used but not defined");
            g->uniqueLabel = it->second;
        }
        // Frame layout: params above the frame pointer, locals below
        for (size_t i = 0; i < fn->params.size(); ++i) fn->params[i]->offset = 4 * int(i) + (4 - std::min(fn->params[i]->type->size(), 4));
        int offset = 0;
        for (Symbol* l : fn->locals) { offset -= std::max(l->type->size(), 1); l->offset = offset; }
        fn->frameSize = -offset;
        unit_.functions.push_back(fn);
        fn_ = nullptr;
    }

    void skipBalancedBraces() {
        expect("{");
        int depth = 1;
        while (depth > 0) {
            if (atEof()) fail(cur(), "Unexpected end of file");
            if (peek("{")) ++depth;
            else if (peek("}")) --depth;
            ++pos_;
        }
    }

    void globalVariable(Type* ty, const Token& nameTok, const DeclSpec& spec) {
        const std::string& name = nameTok.text;
        if (ty->isVoid()) fail(nameTok, "Variable '" + name + "' declared void");
        Symbol* sym;
        if (auto it = globals.find(name); it != globals.end()) {
            sym = it->second;
            if (sym->kind != SymK::Global) fail(nameTok, "'" + name + "' redeclared as a different kind of symbol");
            if (!sameType(sym->type, ty)) fail(nameTok, "Conflicting types for '" + name + "'");
            if (sym->type->isArray() && sym->type->length < 0 && ty->length >= 0) sym->type = ty;
        } else {
            sym = &a_.symbols.emplace_back();
            sym->name = name; sym->kind = SymK::Global; sym->type = ty; sym->label = ".c_" + name; sym->isStatic = spec.isStatic;
            globals[name] = sym;
            scopes_[0][name] = sym;
            unit_.globals.push_back(sym);
        }
        if (accept("=")) {
            if (sym->hasInit) fail(nameTok, "Redefinition of '" + name + "'");
            globalInitializer(sym);
            sym->hasInit = true;
            sym->isDefined = true;
            sym->isExtern = false;
        } else if (!spec.isExtern) {
            sym->isDefined = true;
            sym->isExtern = false;
        } else if (!sym->isDefined) {
            sym->isExtern = true;
        }
    }

    // ------------------------------------------------------------------ declaration specifiers
    bool isTypeName(const Token& t) {
        if (t.kind != TK::Identifier) return false;
        if (kTypeKeywords.count(t.text)) return true;
        Symbol* s = lookup(t.text);
        return s && s->kind == SymK::Typedef;
    }

    DeclSpec declSpec(bool allowStorage) {
        DeclSpec spec;
        int voids = 0, bools = 0, chars = 0, shorts = 0, ints = 0, longs = 0, signeds = 0, unsigneds = 0;
        Type* other = nullptr;
        const Token& start = cur();
        auto any = [&] { return voids + chars + shorts + ints + longs + signeds + unsigneds + bools > 0; };
        while (isTypeName(cur())) {
            const Token& t = cur();
            const std::string& s = t.text;
            if (s == "typedef" || s == "static" || s == "extern" || s == "auto" || s == "register" || s == "inline" || s == "__inline" || s == "_Noreturn") {
                if (!allowStorage) fail(t, "Storage class '" + s + "' is not allowed here");
                if (s == "typedef") spec.isTypedef = true;
                if (s == "static") spec.isStatic = true;
                if (s == "extern") spec.isExtern = true;
                ++pos_;
                continue;
            }
            if (s == "const" || s == "volatile" || s == "restrict" || s == "__restrict") { ++pos_; continue; }
            if (s == "float" || s == "double") fail(t, "Floating point types are not supported");
            if (s == "struct" || s == "union") {
                if (other || any()) fail(t, "Invalid combination of type specifiers");
                bool isUnion = s == "union";
                ++pos_;
                other = structDecl(isUnion);
                continue;
            }
            if (s == "enum") {
                if (other || any()) fail(t, "Invalid combination of type specifiers");
                ++pos_;
                other = enumDecl();
                continue;
            }
            Symbol* td = kTypeKeywords.count(s) ? nullptr : lookup(s);
            if (td && td->kind == SymK::Typedef) {
                // A typedef name only counts as the type if no other type specifier was seen yet
                if (other || any()) break;
                other = td->type;
                ++pos_;
                continue;
            }
            if (other) fail(t, "Invalid combination of type specifiers");
            if (s == "void") ++voids;
            else if (s == "_Bool") ++bools;
            else if (s == "char") ++chars;
            else if (s == "short") ++shorts;
            else if (s == "int") ++ints;
            else if (s == "long") ++longs;
            else if (s == "signed") ++signeds;
            else if (s == "unsigned") ++unsigneds;
            ++pos_;
        }
        if (longs > 1) fail(start, "'long long' is not supported (integers are 32-bit)");
        if (other) { spec.type = other; return spec; }
        int total = voids + bools + chars + shorts + ints + longs + signeds + unsigneds;
        if (total == 0) {
            if (spec.isTypedef || spec.isStatic || spec.isExtern) { spec.type = &tInt; return spec; }   // "static x;" is int
            fail(start, "Expected a type, got '" + start.str() + "'");
        }
        if ((signeds > 0 && unsigneds > 0) || signeds > 1 || unsigneds > 1 || ints > 1 || chars > 1 || shorts > 1)
            fail(start, "Invalid combination of type specifiers");
        if (voids > 0) { if (total != 1) fail(start, "Invalid combination of type specifiers"); spec.type = &tVoid; }
        else if (bools > 0) { if (total != 1) fail(start, "Invalid combination of type specifiers"); spec.type = &tBool; }
        else if (chars > 0) { if (shorts + ints + longs > 0) fail(start, "Invalid combination of type specifiers"); spec.type = unsigneds ? &tUChar : &tChar; }
        else if (shorts > 0) { if (longs > 0) fail(start, "Invalid combination of type specifiers"); spec.type = unsigneds ? &tUShort : &tShort; }
        else spec.type = unsigneds ? &tUInt : &tInt;
        return spec;
    }

    Type* newStruct(const Token* tag, bool isUnion) {
        StructInfo& si = a_.structs.emplace_back();
        if (tag) { si.tag = tag->text; si.hasTag = true; }
        si.isUnion = isUnion;
        Type t{TyK::Struct};
        t.st = &si;
        return a_.type(t);
    }

    Type* structDecl(bool isUnion) {
        const Token* tagTok = nullptr;
        if (cur().kind == TK::Identifier) { tagTok = curp(); ++pos_; }
        if (!peek("{")) {
            if (!tagTok) fail(cur(), "Expected a struct tag or '{'");
            if (Type* found = lookupTag(tagTok->text)) {
                if (!found->isStruct() || found->st->isUnion != isUnion) fail(*tagTok, "'" + tagTok->text + "' defined as wrong kind of tag");
                return found;
            }
            Type* fwd = newStruct(tagTok, isUnion);   // forward declaration: incomplete type in the current scope
            tagScopes_.back()[tagTok->text] = fwd;
            return fwd;
        }
        expect("{");
        Type* ty;
        auto& scope = tagScopes_.back();
        auto it = tagTok ? scope.find(tagTok->text) : scope.end();
        if (it != scope.end() && it->second->isStruct() && !it->second->st->complete) {
            if (it->second->st->isUnion != isUnion) fail(*tagTok, "'" + tagTok->text + "' defined as wrong kind of tag");
            ty = it->second;
        } else {
            if (it != scope.end() && it->second->isStruct() && it->second->st->complete)
                fail(*tagTok, std::string("Redefinition of '") + (isUnion ? "union" : "struct") + " " + tagTok->text + "'");
            ty = newStruct(tagTok, isUnion);
            if (tagTok) tagScopes_.back()[tagTok->text] = ty;
        }
        std::vector<Member*> members;
        while (!accept("}")) {
            DeclSpec spec = declSpec(false);
            if (accept(";")) {
                if (spec.type->isStruct() && spec.type->st->complete) {   // anonymous struct/union member
                    Member* m = &a_.members.emplace_back();
                    m->anonymous = true; m->type = spec.type;
                    members.push_back(m);
                    continue;
                }
                fail(cur(), "Declaration does not declare anything");
            }
            bool first = true;
            while (!accept(";")) {
                if (!first) expect(",");
                first = false;
                const Token* mname;
                Type* mty = declarator(spec.type, mname);
                if (peek(":")) fail(cur(), "Bit-fields are not supported");
                if (!mname) fail(cur(), "Expected a member name");
                if (!mty->isComplete()) fail(*mname, "Member '" + mname->text + "' has incomplete type");
                for (Member* m : members) if (!m->anonymous && m->name == mname->text) fail(*mname, "Duplicate member '" + mname->text + "'");
                Member* m = &a_.members.emplace_back();
                m->name = mname->text; m->type = mty;
                members.push_back(m);
            }
        }
        int size = 0;   // packed layout
        std::vector<Member*> flat;
        for (Member* m : members) {
            int off = isUnion ? 0 : size;
            if (m->anonymous) {   // hoist anonymous members into this struct
                for (Member* inner : m->type->st->members) {
                    Member* c = &a_.members.emplace_back(*inner);
                    c->offset = off + inner->offset;
                    flat.push_back(c);
                }
            } else {
                m->offset = off;
                flat.push_back(m);
            }
            size = isUnion ? std::max(size, m->type->size()) : size + m->type->size();
        }
        ty->st->members = std::move(flat);
        ty->st->size = size;
        ty->st->complete = true;
        return ty;
    }

    Type* enumDecl() {
        const Token* tagTok = nullptr;
        if (cur().kind == TK::Identifier) { tagTok = curp(); ++pos_; }
        Type e{TyK::Int};
        e.isEnum = true;
        Type* ty = a_.type(e);
        if (!peek("{")) {
            if (!tagTok) fail(cur(), "Expected an enum tag or '{'");
            Type* found = lookupTag(tagTok->text);
            if (!found) fail(*tagTok, "Unknown enum '" + tagTok->text + "'");
            return found;
        }
        expect("{");
        int64_t value = 0;
        bool first = true;
        while (!accept("}")) {
            if (!first) {
                expect(",");
                if (accept("}")) break;
            }
            first = false;
            const Token* name = expectIdentifier();
            if (accept("=")) value = evalConst(conditional());
            Symbol* s = &a_.symbols.emplace_back();
            s->name = name->text; s->kind = SymK::EnumConst; s->type = &tInt; s->enumValue = int32_t(value);
            declareInScope(*name, s);
            ++value;
        }
        if (tagTok) tagScopes_.back()[tagTok->text] = ty;
        return ty;
    }

    // ------------------------------------------------------------------ declarators
    Type* declarator(Type* ty, const Token*& name) {
        while (accept("*")) {
            ty = pointerTo(ty);
            while (cur().kind == TK::Identifier && (cur().text == "const" || cur().text == "volatile" || cur().text == "restrict" || cur().text == "__restrict")) ++pos_;
        }
        if (peek("(") && nestedStart(peek2())) {
            // Parse the inner declarator later, once the outer suffix is known
            size_t start = pos_;
            ++pos_;
            const Token* skip;
            declarator(&tInt, skip);
            expect(")");
            ty = typeSuffix(ty);
            size_t end = pos_;
            pos_ = start + 1;
            Type* result = declarator(ty, name);
            expect(")");
            pos_ = end;
            return result;
        }
        // The name may coincide with a typedef name (redeclaration or shadowing)
        name = nullptr;
        if (cur().kind == TK::Identifier && !isKeyword(cur().text)) { name = curp(); ++pos_; }
        return typeSuffix(ty);
    }

    bool nestedStart(const Token& next) { return next.is("*") || next.is("(") || (next.kind == TK::Identifier && !isTypeName(next)); }

    Type* abstractTypeName() {
        DeclSpec spec = declSpec(false);
        const Token* name;
        Type* ty = declarator(spec.type, name);
        if (name) fail(*name, "Unexpected identifier in type name");
        return ty;
    }

    Type* typeSuffix(Type* ty) {
        if (accept("(")) return functionParams(ty);
        if (accept("[")) {
            int length = -1;
            while (cur().kind == TK::Identifier && (cur().text == "static" || cur().text == "const" || cur().text == "restrict")) ++pos_;
            if (!accept("]")) {
                const Token& at = cur();
                int64_t n = evalConst(conditional(), "Array size must be a constant expression (variable-length arrays are not supported)");
                if (n < 0) fail(at, "Array size is negative");
                length = int32_t(n);
                expect("]");
            }
            Type* elem = typeSuffix(ty);
            if (!elem->isComplete()) fail(cur(), "Array has incomplete element type '" + elem->str() + "'");
            return arrayOf(elem, length);
        }
        return ty;
    }

    Type* functionOf(Type* ret, std::vector<Param> ps, bool proto, bool variadic) {
        Type f{TyK::Function};
        f.base = ret; f.params = std::move(ps); f.hasProto = proto; f.variadic = variadic;
        return a_.type(f);
    }

    Type* functionParams(Type* ret) {
        if (accept(")")) return functionOf(ret, {}, false, false);
        if (cur().is("void") && peek2().is(")")) { pos_ += 2; return functionOf(ret, {}, true, false); }
        std::vector<Param> ps;
        bool variadic = false;
        while (true) {
            if (accept("...")) { variadic = true; expect(")"); break; }
            DeclSpec spec = declSpec(false);
            const Token* pname;
            Type* pty = declarator(spec.type, pname);
            if (pty->isVoid()) fail(cur(), "Parameter has void type");
            if (pty->isArray()) pty = pointerTo(pty->base);   // array and function parameters become pointers
            else if (pty->isFunction()) pty = pointerTo(pty);
            ps.push_back({pname ? pname->text : "", pname != nullptr, pty});
            if (accept(")")) break;
            expect(",");
        }
        return functionOf(ret, std::move(ps), true, variadic);
    }

    // ------------------------------------------------------------------ statements
    Node* block(const Token* tok) { Node* n = a_.node(NK::Block, tok); return n; }

    Node* compound(bool newScope = true) {
        Node* node = block(expect("{"));
        if (newScope) enterScope();
        while (!accept("}")) {
            if (atEof()) fail(cur(), "Expected '}' before end of file");
            if (isTypeName(cur()) && !peek2().is(":")) {
                DeclSpec spec = declSpec(true);
                if (spec.isTypedef) { typedefs(spec); continue; }
                node->list.push_back(localDeclaration(spec));
                continue;
            }
            node->list.push_back(statement());
        }
        if (newScope) leaveScope();
        return node;
    }

    Node* localDeclaration(const DeclSpec& spec) {
        Node* blk = block(curp());
        bool first = true;
        while (!accept(";")) {
            if (!first) expect(",");
            first = false;
            const Token* name;
            Type* ty = declarator(spec.type, name);
            if (!name) fail(cur(), "Expected an identifier in declaration");
            if (ty->isFunction()) {
                declareFunction(ty, *name, spec, false);
                scopes_.back()[name->text] = globals[name->text];
                continue;
            }
            if (ty->isVoid()) fail(*name, "Variable '" + name->text + "' declared void");
            if (spec.isExtern) {
                Symbol* g;
                if (auto it = globals.find(name->text); it != globals.end()) g = it->second;
                else {
                    g = &a_.symbols.emplace_back();
                    g->name = name->text; g->kind = SymK::Global; g->type = ty; g->label = ".c_" + name->text; g->isExtern = true;
                    globals[name->text] = g;
                    scopes_[0][name->text] = g;
                    unit_.globals.push_back(g);
                }
                scopes_.back()[name->text] = g;
                continue;
            }
            if (spec.isStatic) {
                Symbol* s = &a_.symbols.emplace_back();
                s->name = name->text; s->kind = SymK::Global; s->type = ty;
                s->label = ".c_" + fn_->name + "__" + name->text + "_" + std::to_string(++staticCounter_);
                s->isStatic = true; s->isDefined = true;
                if (accept("=")) globalInitializer(s);
                else s->initData.assign(size_t(std::max(ty->size(), 0)), 0);
                if (!s->type->isComplete()) fail(*name, "Variable '" + name->text + "' has incomplete type");
                declareInScope(*name, s);
                unit_.globals.push_back(s);
                continue;
            }
            Symbol* local = newLocal(name->text, ty);
            declareInScope(*name, local);
            if (accept("=")) blk->list.push_back(localInitializer(local, name));
            if (!local->type->isComplete()) fail(*name, "Variable '" + name->text + "' has incomplete type '" + local->type->str() + "'");
        }
        return blk;
    }

    Node* emptyNode(const Token* tok) { return a_.node(NK::Empty, tok); }

    Node* statement() {
        const Token* tok = curp();
        if (cur().kind == TK::Identifier && peek2().is(":") && !isKeyword(cur().text)) {
            pos_ += 2;
            if (userLabels_.count(tok->text)) fail(*tok, "Duplicate label '" + tok->text + "'");
            int unique = newLabel();
            userLabels_[tok->text] = unique;
            Node* lbl = a_.node(NK::Label, tok);
            lbl->text = tok->text; lbl->uniqueLabel = unique;
            lbl->bodyStmt = peek("}") ? emptyNode(tok) : statement();
            return lbl;
        }
        if (peek("{")) return compound();
        if (accept(";")) return emptyNode(tok);

        if (tok->kind == TK::Identifier) {
            const std::string& k = tok->text;
            if (k == "return") {
                ++pos_;
                Node* node = a_.node(NK::Return, tok);
                if (!accept(";")) {
                    Node* e = expr();
                    expect(";");
                    if (fn_->type->base->isVoid()) fail(*tok, "Returning a value from a void function");
                    node->lhs = convertForAssignment(e, fn_->type->base, *tok, "return");
                } else if (!fn_->type->base->isVoid() && fn_->name != "main") {
                    fail(*tok, "Non-void function must return a value");
                }
                return node;
            }
            if (k == "if") {
                ++pos_;
                Node* node = a_.node(NK::If, tok);
                expect("(");
                node->cond = toCondition(expr());
                expect(")");
                node->then = statement();
                if (accept("else")) node->els = statement();
                return node;
            }
            if (k == "while") {
                ++pos_;
                Node* node = a_.node(NK::For, tok);
                expect("(");
                node->cond = toCondition(expr());
                expect(")");
                loopBody(node);
                return node;
            }
            if (k == "do") {
                ++pos_;
                Node* node = a_.node(NK::Do, tok);
                loopBody(node);
                if (!accept("while")) fail(cur(), "Expected 'while' after do-statement body");
                expect("(");
                node->cond = toCondition(expr());
                expect(")");
                expect(";");
                return node;
            }
            if (k == "for") {
                ++pos_;
                Node* node = a_.node(NK::For, tok);
                expect("(");
                enterScope();
                if (isTypeName(cur())) {
                    DeclSpec spec = declSpec(true);
                    node->init = localDeclaration(spec);
                } else if (!accept(";")) {
                    node->init = a_.node(NK::ExprStmt, curp());
                    node->init->lhs = expr();
                    expect(";");
                }
                if (!peek(";")) node->cond = toCondition(expr());
                expect(";");
                if (!peek(")")) node->inc = expr();
                expect(")");
                loopBody(node);
                leaveScope();
                return node;
            }
            if (k == "switch") {
                ++pos_;
                Node* node = a_.node(NK::Switch, tok);
                expect("(");
                Node* c = expr();
                if (!c->type->isInteger()) fail(*tok, "Switch quantity is not an integer");
                node->cond = cast(c, promote(c->type));
                expect(")");
                Node* prevSwitch = switch_;
                int prevBrk = brk_;
                switch_ = node;
                node->breakLabel = brk_ = newLabel();
                node->then = statement();
                switch_ = prevSwitch;
                brk_ = prevBrk;
                return node;
            }
            if (k == "case") {
                ++pos_;
                if (!switch_) fail(*tok, "'case' label not within a switch statement");
                int64_t v = evalConst(conditional(), "Case label must be an integer constant");
                expect(":");
                v = normalize(v, switch_->cond->type);
                for (Node* c : switch_->list) if (c->value == v) fail(*tok, "Duplicate case value " + std::to_string(v));
                Node* node = a_.node(NK::Case, tok);
                node->value = v;
                node->uniqueLabel = newLabel();
                switch_->list.push_back(node);
                node->bodyStmt = peek("}") ? emptyNode(tok) : statement();
                return node;
            }
            if (k == "default") {
                ++pos_;
                if (!switch_) fail(*tok, "'default' label not within a switch statement");
                expect(":");
                if (switch_->defaultCase) fail(*tok, "Multiple default labels in one switch");
                Node* node = a_.node(NK::Case, tok);
                node->uniqueLabel = newLabel();
                switch_->defaultCase = node;
                node->bodyStmt = peek("}") ? emptyNode(tok) : statement();
                return node;
            }
            if (k == "break") {
                ++pos_;
                expect(";");
                if (brk_ < 0) fail(*tok, "'break' statement not within a loop or switch");
                Node* n = a_.node(NK::Goto, tok);
                n->uniqueLabel = brk_;
                return n;
            }
            if (k == "continue") {
                ++pos_;
                expect(";");
                if (cont_ < 0) fail(*tok, "'continue' statement not within a loop");
                Node* n = a_.node(NK::Goto, tok);
                n->uniqueLabel = cont_;
                return n;
            }
            if (k == "goto") {
                ++pos_;
                const Token* name = expectIdentifier();
                expect(";");
                Node* n = a_.node(NK::Goto, tok);
                n->text = name->text;
                gotos_.push_back(n);
                return n;
            }
            if (k == "asm" || k == "__asm" || k == "__asm__") {
                ++pos_;
                while (cur().is("volatile") || cur().is("__volatile__")) ++pos_;
                expect("(");
                if (cur().kind != TK::String) fail(cur(), "asm expects a string literal");
                std::string text;
                while (cur().kind == TK::String) { text += cur().text; ++pos_; }
                expect(")");
                expect(";");
                Node* n = a_.node(NK::Asm, tok);
                n->text = std::move(text);
                return n;
            }
        }
        Node* e = expr();
        expect(";");
        Node* n = a_.node(NK::ExprStmt, tok);
        n->lhs = e;
        return n;
    }

    void loopBody(Node* loop) {
        int pb = brk_, pc = cont_;
        loop->breakLabel = brk_ = newLabel();
        loop->continueLabel = cont_ = newLabel();
        loop->then = statement();
        brk_ = pb; cont_ = pc;
    }

    // ------------------------------------------------------------------ initializers
    Init initializerSyntax() {
        const Token* tok = curp();
        if (accept("{")) {
            Init list{tok, true};
            while (!accept("}")) {
                if (peek(".") || peek("[")) fail(cur(), "Designated initializers are not supported");
                list.items.push_back(initializerSyntax());
                if (!accept(",")) { expect("}"); break; }
            }
            return list;
        }
        Init e{tok, false};
        e.expr = assign();
        return e;
    }

    const std::vector<uint8_t>& stringBytes(const Node* str) {
        for (auto& s : unit_.strings) if (s.first == str->text) return s.second;
        fatal("InvalidOperationException");
    }

    // Flattens a (possibly brace-elided) initializer against a type into (offset, type, expr) leaves.
    void flatten(Type* ty, const Init& init, int baseOffset, Leaves& leaves, size_t& cursor, const std::vector<Init>* siblings) {
        if (ty->isArray() || ty->isStruct()) {
            if (ty->isArray() && ty->base->kind == TyK::Char && !init.isList && init.expr->kind == NK::StrLabel) {
                const std::vector<uint8_t>& bytes = stringBytes(init.expr);
                if (ty->length < 0) ty->length = int(bytes.size());   // char s[] = "abc" has 4 elements (type copied)
                else if (int(bytes.size()) - 1 > ty->length) fail(*init.tok, "Initializer string is too long for the array");
                int n = std::min(ty->length, int(bytes.size()));
                for (int i = 0; i < n; ++i) leaves.emplace_back(baseOffset + i, ty->base, num(bytes[size_t(i)], init.tok));
                return;
            }
            if (init.isList) {
                size_t idx = 0;
                fillAggregate(ty, init.items, idx, baseOffset, leaves);
                if (idx < init.items.size()) fail(*init.items[idx].tok, "Excess elements in initializer");
                return;
            }
            if (siblings) {   // brace elision: consume as many sibling items as the aggregate needs
                fillAggregate(ty, *siblings, cursor, baseOffset, leaves);
                --cursor;   // caller increments
                return;
            }
            if (ty->isStruct() && init.expr->type->isStruct()) {   // struct from an expression of the same struct type
                leaves.emplace_back(baseOffset, ty, init.expr);
                return;
            }
            fail(*init.tok, "Invalid initializer for type '" + ty->str() + "'");
        }
        if (init.isList) {
            if (init.items.empty()) fail(*init.tok, "Empty scalar initializer");
            if (init.items.size() > 1) fail(*init.items[1].tok, "Excess elements in scalar initializer");
            size_t dummy = 0;
            flatten(ty, init.items[0], baseOffset, leaves, dummy, nullptr);
            return;
        }
        leaves.emplace_back(baseOffset, ty, init.expr);
    }

    void fillAggregate(Type* ty, const std::vector<Init>& items, size_t& idx, int baseOffset, Leaves& leaves) {
        if (ty->isArray()) {
            int i = 0;
            while (idx < items.size() && (ty->length < 0 || i < ty->length)) {
                size_t c = idx;
                flatten(ty->base, items[idx], baseOffset + i * ty->base->size(), leaves, c, &items);
                idx = c + 1;
                ++i;
            }
            if (ty->length < 0) ty->length = i;   // completes "int a[] = {...}" (type copied first)
            return;
        }
        for (Member* m : ty->st->members) {
            if (idx >= items.size()) break;
            size_t c = idx;
            flatten(m->type, items[idx], baseOffset + m->offset, leaves, c, &items);
            idx = c + 1;
            if (ty->st->isUnion) break;
        }
    }

    Leaves initializerFor(Type*& ty, const Token& nameTok) {
        Init init = initializerSyntax();
        if (ty->isArray() && ty->length < 0) ty = a_.type(*ty);   // the length gets completed by flattening
        Leaves leaves;
        size_t cursor = 0;
        flatten(ty, init, 0, leaves, cursor, nullptr);
        if (ty->isArray() && ty->length < 0) fail(nameTok, "Array size missing in declaration");
        return leaves;
    }

    Node* localInitializer(Symbol* local, const Token* nameTok) {
        Type* ty = local->type;
        Leaves leaves = initializerFor(ty, *nameTok);
        local->type = ty;
        Node* blk = block(nameTok);
        if (ty->isArray() || ty->isStruct()) {
            Node* z = a_.node(NK::MemZero, nameTok);
            z->var = local; z->type = &tVoid;
            Node* s = a_.node(NK::ExprStmt, nameTok);
            s->lhs = z;
            blk->list.push_back(s);
        }
        for (auto& [offset, lty, e] : leaves) {
            // *(lty*)((char*)&local + offset) = expr
            Node* addr = a_.node(NK::Addr, nameTok);
            addr->lhs = varNode(local, nameTok);
            addr->type = pointerTo(local->type);
            Node* bytePtr = cast(addr, pointerTo(&tChar));
            Node* at = offset == 0 ? bytePtr : newAddRaw(bytePtr, num(offset, nameTok), pointerTo(&tChar));
            Node* target = deref(cast(at, pointerTo(lty)), *nameTok);
            Node* s = a_.node(NK::ExprStmt, nameTok);
            s->lhs = newAssign(target, e, *nameTok);
            blk->list.push_back(s);
        }
        return blk;
    }

    void globalInitializer(Symbol* sym) {
        const Token& tok = cur();
        Type* ty = sym->type;
        Leaves leaves = initializerFor(ty, tok);
        sym->type = ty;
        std::vector<uint8_t> data(size_t(std::max(ty->size(), 0)), 0);
        sym->relocs.clear();
        for (auto& [offset, lty, e0] : leaves) {
            if (lty->isStruct()) fail(*e0->tok, "Initializer element is not a compile-time constant");
            Node* e = convertForAssignment(e0, lty, *e0->tok, "initialization");
            std::string label;
            int64_t value = evalWithLabel(e, label, "Initializer element is not a compile-time constant");
            if (!label.empty()) {
                if (lty->size() != 4) fail(*e0->tok, "Address constant does not fit in the initialized type");
                sym->relocs.push_back({offset, label, value});
                continue;
            }
            int size = lty->size();
            if (offset < 0 || size_t(offset + size) > data.size()) fatal("IndexOutOfRangeException");
            for (int i = 0; i < size; ++i) data[size_t(offset + i)] = uint8_t(uint64_t(value) >> (8 * (size - 1 - i)));
        }
        sym->initData = std::move(data);
    }

    // ------------------------------------------------------------------ expressions
    Node* expr() {
        Node* node = assign();
        while (peek(",")) {
            const Token* tok = curp();
            ++pos_;
            Node* rhs = assign();
            Node* c = a_.node(NK::Comma, tok);
            c->lhs = node; c->rhs = rhs; c->type = decay(rhs->type);
            node = c;
        }
        return node;
    }

    Node* assign() {
        Node* lhs = conditional();
        const Token* tok = curp();
        if (accept("=")) return newAssign(lhs, assign(), *tok);
        if (tok->kind == TK::Punct) {
            static const std::pair<const char*, NK> ops[] = {
                {"+=", NK::Add}, {"-=", NK::Sub}, {"*=", NK::Mul}, {"/=", NK::Div}, {"%=", NK::Mod},
                {"&=", NK::BitAnd}, {"|=", NK::BitOr}, {"^=", NK::BitXor}, {"<<=", NK::Shl}, {">>=", NK::Shr}};
            for (auto& [s, op] : ops)
                if (tok->text == s) { ++pos_; return newCompoundAssign(lhs, op, assign(), *tok); }
        }
        return lhs;
    }

    Node* conditional() {
        Node* c = binary(0);
        const Token* tok = curp();
        if (!accept("?")) return c;
        Node* a = expr();
        expect(":");
        Node* b = conditional();
        Node* node = a_.node(NK::Cond, tok);
        node->cond = toCondition(c);
        Type *ta = decay(a->type), *tb = decay(b->type);
        if (ta->isVoid() || tb->isVoid()) node->type = &tVoid;
        else if (ta->isInteger() && tb->isInteger()) {
            Type* common = commonType(ta, tb);
            a = cast(a, common);
            b = cast(b, common);
            node->type = common;
        }
        else if (ta->isPointer() && tb->isPointer()) node->type = ta->base->isVoid() ? tb : ta;
        else if (ta->isPointer() && isNullConstant(b)) { node->type = ta; b = cast(b, ta); }
        else if (tb->isPointer() && isNullConstant(a)) { node->type = tb; a = cast(a, tb); }
        else if (ta->isStruct() && tb->isStruct() && ta->st == tb->st) node->type = ta;
        else fail(*tok, "Type mismatch in conditional expression ('" + ta->str() + "' and '" + tb->str() + "')");
        node->then = a;
        node->els = b;
        return node;
    }

    // Precedence climbing over the reference's ten left-associative levels (|| ... * / %): the same trees,
    // built in the same order, with two stack frames per nesting level instead of eleven (200-deep parentheses).
    static int binaryLevel(const Token& t) {
        static const std::vector<std::vector<std::string_view>> levels = {
            {"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<", ">", "<=", ">="}, {"<<", ">>"}, {"+", "-"}, {"*", "/", "%"}};
        if (t.kind != TK::Punct || t.text.size() > 2 || std::string_view("|&^=!<>+-*/%").find(t.text[0]) == std::string_view::npos) return -1;
        for (size_t l = 0; l < levels.size(); ++l)
            if (std::find(levels[l].begin(), levels[l].end(), t.text) != levels[l].end()) return int(l);
        return -1;
    }

    Node* binary(int minLevel) {
        Node* lhs = castExpr();
        while (true) {
            const Token* tok = curp();
            int level = binaryLevel(*tok);
            if (level < minLevel) return lhs;
            ++pos_;
            Node* rhs = binary(level + 1);
            lhs = binaryOp(tok->text, lhs, rhs, *tok);
        }
    }

    Node* binaryOp(const std::string& op, Node* lhs, Node* rhs, const Token& tok) {
        if (op == "+") return newAdd(lhs, rhs, tok);
        if (op == "-") return newSub(lhs, rhs, tok);
        if (op == "*") return arith(NK::Mul, lhs, rhs, tok);
        if (op == "/") return arith(NK::Div, lhs, rhs, tok);
        if (op == "%") return arith(NK::Mod, lhs, rhs, tok);
        if (op == "&") return arith(NK::BitAnd, lhs, rhs, tok);
        if (op == "|") return arith(NK::BitOr, lhs, rhs, tok);
        if (op == "^") return arith(NK::BitXor, lhs, rhs, tok);
        if (op == "<<") return shift(NK::Shl, lhs, rhs, tok);
        if (op == ">>") return shift(NK::Shr, lhs, rhs, tok);
        if (op == "==") return compare(NK::Eq, lhs, rhs, tok);
        if (op == "!=") return compare(NK::Ne, lhs, rhs, tok);
        if (op == "<") return compare(NK::Lt, lhs, rhs, tok);
        if (op == "<=") return compare(NK::Le, lhs, rhs, tok);
        if (op == ">") return compare(NK::Lt, rhs, lhs, tok);
        if (op == ">=") return compare(NK::Le, rhs, lhs, tok);
        if (op == "&&" || op == "||") {
            Node* n = a_.node(op == "&&" ? NK::LogAnd : NK::LogOr, &tok);
            n->lhs = toCondition(lhs);
            n->rhs = toCondition(rhs);
            n->type = &tInt;
            return n;
        }
        fail(tok, "Unknown operator '" + op + "'");
    }

    Node* castExpr() {
        if (peek("(") && isTypeName(peek2())) {
            const Token* tok = curp();
            ++pos_;
            Type* ty = abstractTypeName();
            expect(")");
            if (peek("{")) fail(cur(), "Compound literals are not supported");
            Node* operand = castExpr();
            return explicitCast(operand, ty, *tok);
        }
        return unary();
    }

    Node* unary() {
        const Token* tok = curp();
        if (accept("+")) {
            Node* e = rvalue(castExpr(), *tok);
            if (!e->type->isInteger()) fail(*tok, "Invalid operand to unary '+'");
            return cast(e, promote(e->type));
        }
        if (accept("-") || accept("~")) {
            bool neg = tok->text == "-";
            Node* e = rvalue(castExpr(), *tok);
            if (!e->type->isInteger()) fail(*tok, neg ? "Invalid operand to unary '-'" : "Invalid operand to '~'");
            Type* t = promote(e->type);
            Node* n = a_.node(neg ? NK::Neg : NK::BitNot, tok);
            n->lhs = cast(e, t);
            n->type = t;
            return n;
        }
        if (accept("!")) {
            Node* e = toCondition(castExpr());
            Node* n = a_.node(NK::LogNot, tok);
            n->lhs = e; n->type = &tInt;
            return n;
        }
        if (accept("&")) return addressOf(castExpr(), *tok);
        if (accept("*")) return deref(castExpr(), *tok);
        if (accept("++")) return newCompoundAssign(unary(), NK::Add, num(1, tok), *tok);
        if (accept("--")) return newCompoundAssign(unary(), NK::Sub, num(1, tok), *tok);
        if (accept("sizeof")) {
            if (peek("(") && isTypeName(peek2())) {
                ++pos_;
                Type* ty = abstractTypeName();
                expect(")");
                return sizeOf(ty, tok);
            }
            Node* e = unary();
            return sizeOf(e->type, tok);
        }
        return postfix();
    }

    Node* sizeOf(Type* ty, const Token* tok) {
        if (ty->isFunction()) fail(*tok, "Invalid application of 'sizeof' to a function type");
        if (!ty->isComplete() && !ty->isVoid()) fail(*tok, "Invalid application of 'sizeof' to incomplete type '" + ty->str() + "'");
        Node* n = a_.node(NK::Num, tok);
        n->value = ty->size(); n->type = &tUInt;
        return n;
    }

    Node* postfix() {
        Node* node = primary();
        while (true) {
            const Token* tok = curp();
            if (accept("[")) {
                Node* idx = expr();
                expect("]");
                node = deref(newAdd(node, idx, *tok), *tok);
                continue;
            }
            if (accept("(")) { node = call(node, tok); continue; }
            if (accept(".")) { node = memberAccess(node, *expectIdentifier(), tok); continue; }
            if (accept("->")) { Node* d = deref(node, *tok); node = memberAccess(d, *expectIdentifier(), tok); continue; }
            if (accept("++")) { node = postIncDec(node, NK::Add, *tok); continue; }
            if (accept("--")) { node = postIncDec(node, NK::Sub, *tok); continue; }
            return node;
        }
    }

    Node* call(Node* fn, const Token* tok) {
        Type* fty = fn->type->isFunction() ? fn->type : fn->type->isPointer() && fn->type->base->isFunction() ? fn->type->base : nullptr;
        if (!fty) fail(*tok, "Called object is not a function or function pointer");
        std::vector<Node*> args;
        if (!accept(")")) {
            while (true) {
                args.push_back(assign());
                if (accept(")")) break;
                expect(",");
            }
        }
        std::string name = fn->kind == NK::Var ? fn->var->name : "function";
        if (fty->hasProto && (args.size() < fty->params.size() || (!fty->variadic && args.size() > fty->params.size())))
            fail(*tok, "Wrong number of arguments to '" + name + "': expected " + std::to_string(fty->params.size()) + ", got " + std::to_string(args.size()));
        for (size_t i = 0; i < args.size(); ++i) {
            Node* a = args[i];
            if (a->type->isStruct()) fail(*a->tok, "Passing structs by value is not supported; pass a pointer instead");
            if (fty->hasProto && i < fty->params.size())
                args[i] = convertForAssignment(a, fty->params[i].type, *a->tok, "argument " + std::to_string(i + 1) + " of '" + name + "'");
            else {
                a = rvalue(a, *a->tok);
                args[i] = a->type->isInteger() ? cast(a, promote(a->type)) : a;
            }
        }
        Node* node = a_.node(NK::Call, tok);
        node->lhs = fn; node->list = std::move(args); node->funcType = fty; node->type = fty->base;
        if (fn->kind == NK::Var && fn->var->kind == SymK::Function) {
            node->directCallee = fn->var;
            if (fn_) fn_->called.add(fn->var->label);
        }
        return node;
    }

    Node* primary() {
        const Token* tok = curp();
        if (accept("(")) {
            if (peek("{")) fail(*tok, "Statement expressions are not supported");
            Node* e = expr();
            expect(")");
            return e;
        }
        if (tok->kind == TK::Number) {
            ++pos_;
            uint32_t v; bool uns; std::string err;
            if (!tryParseInt(tok->text, v, uns, err)) fail(*tok, err);
            Node* n = a_.node(NK::Num, tok);
            n->value = uns ? int64_t(v) : int64_t(int32_t(v));
            n->type = uns ? &tUInt : &tInt;
            return n;
        }
        if (tok->kind == TK::Char) {
            ++pos_;
            Node* n = a_.node(NK::Num, tok);
            n->value = tok->text.empty() ? 0 : int8_t(lowBytes(tok->text)[0]);
            n->type = &tInt;
            return n;
        }
        if (tok->kind == TK::String) {
            std::string s;   // adjacent string literals concatenate
            while (cur().kind == TK::String) { s += cur().text; ++pos_; }
            std::vector<uint8_t> bytes = lowBytes(s);
            bytes.push_back(0);
            std::string label = ".c__str" + std::to_string(unit_.strings.size());
            int len = int(bytes.size());
            unit_.strings.emplace_back(label, std::move(bytes));
            Node* n = a_.node(NK::StrLabel, tok);
            n->text = label;
            n->type = arrayOf(&tChar, len);
            return n;
        }
        if (tok->kind == TK::Identifier) {
            if (isKeyword(tok->text)) fail(*tok, "Expected an expression, got '" + tok->text + "'");
            ++pos_;
            Symbol* sym = lookup(tok->text);
            if (!sym) {
                if (peek("(")) fail(*tok, "Implicit declaration of function '" + tok->text + "' (declare it or include the right header)");
                fail(*tok, "Undeclared identifier '" + tok->text + "'");
            }
            switch (sym->kind) {
                case SymK::EnumConst: { Node* n = a_.node(NK::Num, tok); n->value = sym->enumValue; n->type = &tInt; return n; }
                case SymK::Typedef: fail(*tok, "Unexpected type name '" + tok->text + "'");
                case SymK::Function:
                case SymK::Global:
                    if (fn_) fn_->called.add(sym->label);
                    sym->isReferenced = true;
                    return varNode(sym, tok);
                default: return varNode(sym, tok);
            }
        }
        fail(*tok, "Expected an expression, got '" + tok->str() + "'");
    }

    // ------------------------------------------------------------------ typed node construction
    Node* varNode(Symbol* s, const Token* tok) { Node* n = a_.node(NK::Var, tok); n->var = s; n->type = s->type; return n; }
    Node* num(int64_t v, const Token* tok) { Node* n = a_.node(NK::Num, tok); n->value = v; n->type = &tInt; return n; }

    static Type* promote(Type* t) { return t->kind == TyK::Int ? (t->isEnum ? &tInt : t) : &tInt; }
    static Type* commonType(Type* a, Type* b) { return promote(a)->uns || promote(b)->uns ? &tUInt : &tInt; }

    Node* rvalue(Node* n, const Token& tok) {
        if (n->type->isVoid()) fail(tok, "Void value not ignored as it ought to be");
        if (n->type->isStruct()) fail(tok, "Struct value used where a scalar is required");
        return n;
    }

    Node* toCondition(Node* n) {
        if (!decay(n->type)->isScalar()) fail(*n->tok, "Used '" + n->type->str() + "' where a scalar condition is required");
        return n;
    }

    Node* cast(Node* n, Type* to) {
        if (sameType(decay(n->type), to) && !to->isBool) return n;
        if (n->kind == NK::Num && to->isInteger()) {   // folding keeps constant expressions simple
            Node* c = a_.node(NK::Num, n->tok);
            c->value = normalize(n->value, to); c->type = to;
            return c;
        }
        Node* c = a_.node(NK::Cast, n->tok);
        c->lhs = n; c->type = to;
        return c;
    }

    Node* explicitCast(Node* e, Type* to, const Token& tok) {
        if (to->isVoid()) { Node* c = a_.node(NK::Cast, &tok); c->lhs = e; c->type = &tVoid; return c; }
        if (to->isStruct() || to->isArray() || to->isFunction()) fail(tok, "Cannot cast to '" + to->str() + "'");
        if (!decay(e->type)->isScalar()) fail(tok, "Cannot cast '" + e->type->str() + "' to '" + to->str() + "'");
        return cast(e, to);
    }

    static bool isNullConstant(const Node* n) {
        return (n->kind == NK::Num && n->value == 0) ||
               (n->kind == NK::Cast && n->type->isPointer() && n->type->base->isVoid() && isNullConstant(n->lhs));
    }

    Node* convertForAssignment(Node* e, Type* to, const Token& tok, const std::string& context) {
        Type* from = decay(e->type);
        if (to->isStruct()) {
            if (from->isStruct() && from->st == to->st) return e;
            fail(tok, "Incompatible types in " + context + ": '" + from->str() + "' to '" + to->str() + "'");
        }
        if (from->isVoid()) fail(tok, "Void value not ignored as it ought to be");
        if (from->isStruct()) fail(tok, "Incompatible types in " + context + ": '" + from->str() + "' to '" + to->str() + "'");
        if (to->isPointer()) {
            if (from->isPointer()) {
                bool ok = to->base->isVoid() || from->base->isVoid() || sameType(to->base, from->base) ||
                          (to->base->isInteger() && from->base->isInteger() && to->base->size() == from->base->size());
                if (!ok) fail(tok, "Incompatible pointer types in " + context + ": '" + from->str() + "' to '" + to->str() + "'");
                return cast(e, to);
            }
            if (isNullConstant(e)) return cast(e, to);
            if (from->isInteger()) fail(tok, "Making a pointer from an integer without a cast in " + context);
        }
        if (to->isInteger() && from->isPointer() && !to->isBool) fail(tok, "Making an integer from a pointer without a cast in " + context);
        return cast(e, to);
    }

    Node* deref(Node* e, const Token& tok) {
        Type* t = decay(e->type);
        if (!t->isPointer()) fail(tok, "Cannot dereference non-pointer type '" + e->type->str() + "'");
        if (t->base->isFunction()) return e;   // *fp is the function itself
        if (t->base->isVoid()) fail(tok, "Dereferencing a 'void *' pointer");
        Node* n = a_.node(NK::Deref, &tok);
        n->lhs = e; n->type = t->base;
        return n;
    }

    Node* addressOf(Node* e, const Token& tok) {
        if (!(e->kind == NK::Var && e->var->kind == SymK::Function) && !isLValue(e)) fail(tok, "Cannot take the address of an rvalue");
        Node* n = a_.node(NK::Addr, &tok);
        n->lhs = e; n->type = pointerTo(e->type);
        return n;
    }

    static bool isLValue(const Node* e) {
        switch (e->kind) {
            case NK::Var: return e->var->kind != SymK::Function;
            case NK::Deref: return true;
            case NK::Member: return isLValue(e->lhs);
            case NK::StrLabel: return true;
            default: return false;
        }
    }

    Node* memberAccess(Node* obj, const Token& name, const Token* tok) {
        if (!obj->type->isStruct()) fail(*tok, "Member reference base type '" + obj->type->str() + "' is not a struct or union");
        if (!obj->type->st->complete) fail(*tok, "Incomplete type '" + obj->type->str() + "'");
        Member* m = obj->type->st->find(name.text);
        if (!m) fail(name, "No member named '" + name.text + "' in '" + obj->type->str() + "'");
        Node* n = a_.node(NK::Member, tok);
        n->lhs = obj; n->member = m; n->type = m->type;
        return n;
    }

    Node* binNode(NK k, const Token* tok, Node* l, Node* r, Type* t) { Node* n = a_.node(k, tok); n->lhs = l; n->rhs = r; n->type = t; return n; }

    Node* arith(NK kind, Node* lhs, Node* rhs, const Token& tok) {
        Type* lt = decay(rvalue(lhs, tok)->type);
        Type* rt = decay(rvalue(rhs, tok)->type);
        if (!lt->isInteger() || !rt->isInteger()) fail(tok, "Invalid operands to binary operator ('" + lt->str() + "' and '" + rt->str() + "')");
        Type* common = commonType(lt, rt);
        if ((kind == NK::Div || kind == NK::Mod) && rhs->kind == NK::Num && rhs->value == 0) fail(tok, "Division by zero");
        Node* l = cast(lhs, common);
        return binNode(kind, &tok, l, cast(rhs, common), common);
    }

    Node* shift(NK kind, Node* lhs, Node* rhs, const Token& tok) {
        Type* lt = decay(rvalue(lhs, tok)->type);
        Type* rt = decay(rvalue(rhs, tok)->type);
        if (!lt->isInteger() || !rt->isInteger()) fail(tok, "Invalid operands to shift ('" + lt->str() + "' and '" + rt->str() + "')");
        Type* t = promote(lt);
        Node* l = cast(lhs, t);
        return binNode(kind, &tok, l, cast(rhs, &tInt), t);
    }

    Node* compare(NK kind, Node* lhs, Node* rhs, const Token& tok) {
        Type* lt = decay(rvalue(lhs, tok)->type);
        Type* rt = decay(rvalue(rhs, tok)->type);
        if (lt->isInteger() && rt->isInteger()) {
            Type* common = commonType(lt, rt);
            Node* l = cast(lhs, common);
            return binNode(kind, &tok, l, cast(rhs, common), &tInt);
        }
        if ((lt->isPointer() && rt->isPointer()) || (lt->isPointer() && isNullConstant(rhs)) || (rt->isPointer() && isNullConstant(lhs))) {
            Node* l = cast(lhs, &tUInt);
            return binNode(kind, &tok, l, cast(rhs, &tUInt), &tInt);
        }
        if ((lt->isPointer() && rt->isInteger()) || (lt->isInteger() && rt->isPointer())) fail(tok, "Comparison between pointer and integer");
        fail(tok, "Invalid operands to comparison ('" + lt->str() + "' and '" + rt->str() + "')");
    }

    Node* newAddRaw(Node* ptr, Node* offset, Type* resultType) {
        Node* l = cast(ptr, resultType);
        return binNode(NK::Add, ptr->tok, l, cast(offset, &tInt), resultType);
    }

    Node* scale(Node* idx, int size, const Token& tok) {
        idx = cast(idx, &tInt);
        if (size == 1) return idx;
        if (idx->kind == NK::Num) {
            Node* n = a_.node(NK::Num, &tok);
            n->value = int32_t(uint64_t(idx->value) * uint64_t(int64_t(size)));
            n->type = &tInt;
            return n;
        }
        return binNode(NK::Mul, &tok, idx, num(size, &tok), &tInt);
    }

    Node* newAdd(Node* lhs, Node* rhs, const Token& tok) {
        Type* lt = decay(rvalue(lhs, tok)->type);
        Type* rt = decay(rvalue(rhs, tok)->type);
        if (lt->isInteger() && rt->isInteger()) return arith(NK::Add, lhs, rhs, tok);
        if (lt->isInteger() && rt->isPointer()) { std::swap(lhs, rhs); std::swap(lt, rt); }
        if (lt->isPointer() && rt->isInteger()) {
            if (lt->base->isFunction() || (!lt->base->isComplete() && !lt->base->isVoid()))
                fail(tok, "Arithmetic on a pointer to an incomplete type '" + lt->base->str() + "'");
            Node* l = cast(lhs, lt);
            return binNode(NK::Add, &tok, l, scale(rhs, lt->base->size(), tok), lt);
        }
        fail(tok, "Invalid operands to '+' ('" + lt->str() + "' and '" + rt->str() + "')");
    }

    Node* newSub(Node* lhs, Node* rhs, const Token& tok) {
        Type* lt = decay(rvalue(lhs, tok)->type);
        Type* rt = decay(rvalue(rhs, tok)->type);
        if (lt->isInteger() && rt->isInteger()) return arith(NK::Sub, lhs, rhs, tok);
        if (lt->isPointer() && rt->isInteger()) {
            if (lt->base->isFunction() || (!lt->base->isComplete() && !lt->base->isVoid()))
                fail(tok, "Arithmetic on a pointer to an incomplete type '" + lt->base->str() + "'");
            Node* l = cast(lhs, lt);
            return binNode(NK::Sub, &tok, l, scale(rhs, lt->base->size(), tok), lt);
        }
        if (lt->isPointer() && rt->isPointer()) {
            if (!sameType(lt->base, rt->base)) fail(tok, "Subtracting incompatible pointer types ('" + lt->str() + "' and '" + rt->str() + "')");
            Node* l = cast(lhs, &tInt);
            Node* diff = binNode(NK::Sub, &tok, l, cast(rhs, &tInt), &tInt);
            int size = lt->base->size();
            if (size == 1) return diff;
            return binNode(NK::Div, &tok, diff, num(size, &tok), &tInt);
        }
        fail(tok, "Invalid operands to '-' ('" + lt->str() + "' and '" + rt->str() + "')");
    }

    Node* newAssign(Node* lhs, Node* rhs, const Token& tok) {
        if (!isLValue(lhs) || lhs->kind == NK::StrLabel) fail(tok, "Expression is not assignable");
        if (lhs->type->isArray()) fail(tok, "Array type is not assignable");
        if (lhs->type->isStruct() && !lhs->type->st->complete) fail(tok, "Assignment to incomplete type");
        Node* value = convertForAssignment(rhs, lhs->type, tok, "assignment");
        return binNode(NK::Assign, &tok, lhs, value, lhs->type);
    }

    // A op= B  becomes  A = A op B for plain variables, otherwise  (tmp = &A, *tmp = *tmp op B).
    Node* newCompoundAssign(Node* lhs, NK op, Node* rhs, const Token& tok) {
        if (!isLValue(lhs) || lhs->type->isArray() || lhs->type->isStruct()) fail(tok, "Expression is not assignable");
        if (lhs->kind == NK::Var && lhs->var->kind != SymK::Function)
            return newAssign(lhs, opFor(op, varNode(lhs->var, &tok), rhs, tok), tok);
        Symbol* tmp = newLocal("", pointerTo(lhs->type));
        Node* tv = varNode(tmp, &tok);
        Node* setTmp = binNode(NK::Assign, &tok, tv, addressOf(lhs, tok), tmp->type);
        auto target = [&] { Node* d = a_.node(NK::Deref, &tok); d->lhs = varNode(tmp, &tok); d->type = lhs->type; return d; };
        Node* t1 = target();
        Node* t2 = target();
        Node* asg = newAssign(t1, opFor(op, t2, rhs, tok), tok);
        return binNode(NK::Comma, &tok, setTmp, asg, asg->type);
    }

    Node* opFor(NK op, Node* a, Node* b, const Token& tok) {
        switch (op) {
            case NK::Add: return newAdd(a, b, tok);
            case NK::Sub: return newSub(a, b, tok);
            case NK::Shl: case NK::Shr: return shift(op, a, b, tok);
            default: return arith(op, a, b, tok);
        }
    }

    // A++  becomes  (typeof A)((A += 1) - 1).
    Node* postIncDec(Node* lhs, NK op, const Token& tok) {
        if (!isLValue(lhs) || !lhs->type->isScalar()) fail(tok, "Expression is not assignable");
        Node* updated = newCompoundAssign(lhs, op, num(1, &tok), tok);
        Node* undone = op == NK::Add ? newSub(updated, num(1, &tok), tok) : newAdd(updated, num(1, &tok), tok);
        return cast(undone, lhs->type);
    }

    // ------------------------------------------------------------------ constant evaluation
    int64_t evalConst(Node* n, const std::string& message = "Expected an integer constant expression") {
        std::string label;
        int64_t v = evalWithLabel(n, label, message);
        if (!label.empty()) fail(*n->tok, message);
        return v;
    }

    // Address constants return the label plus the value as the addend.
    int64_t evalWithLabel(Node* n, std::string& label, const std::string& message) {
        label.clear();
        switch (n->kind) {
            case NK::Num: return n->value;
            case NK::StrLabel: label = n->text; return 0;
            case NK::Var:
                if (n->var->kind == SymK::Function || (n->var->kind == SymK::Global && n->type->isArray())) { label = n->var->label; return 0; }
                break;
            case NK::Addr: return evalAddress(n->lhs, label, message);
            case NK::Cast: {
                int64_t v = evalWithLabel(n->lhs, label, message);
                if (!label.empty()) {
                    if (n->type->size() != 4) fail(*n->tok, message);
                    return v;
                }
                return n->type->isVoid() ? 0 : normalize(v, n->type);
            }
            case NK::Add: case NK::Sub: {
                std::string la, lb;
                int64_t a = evalWithLabel(n->lhs, la, message);
                int64_t b = evalWithLabel(n->rhs, lb, message);
                if (!lb.empty()) fail(*n->tok, message);
                label = la;
                int64_t r = int64_t(n->kind == NK::Add ? uint64_t(a) + uint64_t(b) : uint64_t(a) - uint64_t(b));
                return !la.empty() ? r : normalize(r, n->type);
            }
            case NK::Cond: {
                int64_t c = evalConst(n->cond, message);
                return evalWithLabel(c != 0 ? n->then : n->els, label, message);
            }
            case NK::Neg: return normalize(int64_t(0 - uint64_t(evalConst(n->lhs, message))), n->type);
            case NK::BitNot: return normalize(~evalConst(n->lhs, message), n->type);
            case NK::LogNot: return evalConst(n->lhs, message) == 0 ? 1 : 0;
            case NK::LogAnd: return evalConst(n->lhs, message) != 0 && evalConst(n->rhs, message) != 0 ? 1 : 0;
            case NK::LogOr: return evalConst(n->lhs, message) != 0 || evalConst(n->rhs, message) != 0 ? 1 : 0;
            case NK::Mul: case NK::Div: case NK::Mod: case NK::BitAnd: case NK::BitOr: case NK::BitXor:
            case NK::Shl: case NK::Shr: case NK::Eq: case NK::Ne: case NK::Lt: case NK::Le: {
                int64_t a = evalConst(n->lhs, message);
                int64_t b = evalConst(n->rhs, message);
                bool uns = decay(n->lhs->type)->uns;
                uint32_t ua = uint32_t(a), ub = uint32_t(b);
                int32_t ia = int32_t(a), ib = int32_t(b);
                int64_t r;
                switch (n->kind) {
                    case NK::Mul: r = int64_t(uint64_t(a) * uint64_t(b)); break;
                    case NK::Div:
                    case NK::Mod:
                        if (b == 0) fail(*n->tok, "Division by zero in constant expression");
                        if ((uns ? ub : uint32_t(ib)) == 0) fatal("DivideByZeroException");   // b != 0, its low 32 bits are
                        if (uns) r = n->kind == NK::Div ? ua / ub : ua % ub;
                        else if (ia == INT32_MIN && ib == -1) fatal("OverflowException");
                        else r = n->kind == NK::Div ? ia / ib : ia % ib;
                        break;
                    case NK::BitAnd: r = a & b; break;
                    case NK::BitOr: r = a | b; break;
                    case NK::BitXor: r = a ^ b; break;
                    case NK::Shl: r = int64_t(uint32_t(ua << (b & 31))); break;
                    case NK::Shr: r = uns ? int64_t(ua >> (b & 31)) : int64_t(ia >> (b & 31)); break;
                    case NK::Eq: return ua == ub ? 1 : 0;
                    case NK::Ne: return ua != ub ? 1 : 0;
                    case NK::Lt: return (uns ? ua < ub : ia < ib) ? 1 : 0;
                    case NK::Le: return (uns ? ua <= ub : ia <= ib) ? 1 : 0;
                    default: r = 0; break;
                }
                return normalize(r, n->type);
            }
            default: break;
        }
        fail(*n->tok, message);
    }

    int64_t evalAddress(Node* n, std::string& label, const std::string& message) {
        switch (n->kind) {
            case NK::Var:
                if (n->var->kind == SymK::Global || n->var->kind == SymK::Function) { label = n->var->label; return 0; }
                break;
            case NK::StrLabel: label = n->text; return 0;
            case NK::Deref: return evalWithLabel(n->lhs, label, message);
            case NK::Member: return evalAddress(n->lhs, label, message) + n->member->offset;
            default: break;
        }
        fail(*n->tok, message);
    }

    // ------------------------------------------------------------------ scopes and symbols
    Symbol* newLocal(const std::string& name, Type* ty) {
        if (!fn_) fatal("NullReferenceException");   // e.g. "x += 1" in a global's array size
        Symbol* s = &a_.symbols.emplace_back();
        s->name = name; s->kind = SymK::Local; s->type = ty;
        fn_->locals.push_back(s);
        return s;
    }
    void enterScope() { scopes_.emplace_back(); tagScopes_.emplace_back(); }
    void leaveScope() { scopes_.pop_back(); tagScopes_.pop_back(); }
    void declareInScope(const Token& name, Symbol* sym) {
        auto& scope = scopes_.back();
        if (auto it = scope.find(name.text); it != scope.end()) {
            Symbol* e = it->second;
            bool sameTypedef = e->kind == SymK::Typedef && sym->kind == SymK::Typedef && sameType(e->type, sym->type);
            if (!sameTypedef) fail(name, "Redefinition of '" + name.text + "'");
        }
        scope[name.text] = sym;
    }
    Symbol* lookup(const std::string& name) {
        for (size_t i = scopes_.size(); i-- > 0;)
            if (auto it = scopes_[i].find(name); it != scopes_[i].end()) return it->second;
        return nullptr;
    }
    Type* lookupTag(const std::string& name) {
        for (size_t i = tagScopes_.size(); i-- > 0;)
            if (auto it = tagScopes_[i].find(name); it != tagScopes_[i].end()) return it->second;
        return nullptr;
    }
};

ParseResult parse(Arena& a, Unit& unit, Errors& errors, TokenList& user, TokenList& library,
                  const std::function<void()>& between) {
    Parser p(a, unit, errors);
    p.parseUnit(user, false);
    between();   // the runtime library is preprocessed after the user's code is parsed
    p.parseUnit(library, true);
    auto it = p.globals.find("main");
    Symbol* main = it != p.globals.end() && it->second->kind == SymK::Function && it->second->isDefined ? it->second : nullptr;
    return {p.labelCounter, main};
}

} // namespace c93::cc
