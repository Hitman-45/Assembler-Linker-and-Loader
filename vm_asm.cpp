// vmasm.cpp
// VM Assembler — Modules 1–3 (C++17)
// - Tokenize, parse assembly, generate 8-byte instructions
// - Sections: .text, .data, globals via .global
// - Simple macro processor (.macro NAME N ... .endm) with $1..$N params
// - Emit .vmo object file with header + .text + .data + symbol table + relocation table

#include <bits/stdc++.h>
using namespace std;

// -----------------------------
// Tokenizer
// -----------------------------
enum class TKind {
    WS, COMMENT, DIRECTIVE, LABEL, REGISTER, HEX, BIN, INT,
    IDENT, COMMA, LBRACK, RBRACK, PLUS, NEWLINE, STRING, EOF_TOK
};

struct Token {
    TKind kind; string value; int line; int col;
};

struct Rule { TKind kind; regex re; };

// REGISTER regex changed to allow 0..31 (r0..r31 or x0..x31) to match parseReg check
static const vector<Rule> TOKEN_RULES = {
    {TKind::WS,        regex(R"([ \t]+)")},
    {TKind::COMMENT,   regex(R"(;.*)")},
    {TKind::DIRECTIVE, regex(R"(\.[A-Za-z_][A-Za-z0-9_]*)")},
    {TKind::LABEL,     regex(R"([A-Za-z_][A-Za-z0-9_]*:)")},
    {TKind::REGISTER,  regex(R"((?:r|x)(?:[0-9]|[12][0-9]|3[01])\b)")},
    {TKind::HEX,       regex(R"(0x[0-9A-Fa-f]+)")},
    {TKind::BIN,       regex(R"(0b[01]+)")},
    {TKind::INT,       regex(R"(-?\d+)")},
    {TKind::IDENT,     regex(R"([A-Za-z_][A-Za-z0-9_]*)")},
    {TKind::COMMA,     regex(R"(,)")},
    {TKind::LBRACK,    regex(R"(\[)")},
    {TKind::RBRACK,    regex(R"(\])")},
    {TKind::PLUS,      regex(R"(\+)")},
    {TKind::STRING,    regex(R"("([^"\\]|\\.)*")")},
    {TKind::NEWLINE,   regex(R"(\n+)")}
};

static string kindName(TKind k){
    switch(k){
        case TKind::WS: return "WS"; case TKind::COMMENT: return "COMMENT";
        case TKind::DIRECTIVE: return "DIRECTIVE"; case TKind::LABEL: return "LABEL";
        case TKind::REGISTER: return "REGISTER"; case TKind::HEX: return "HEX";
        case TKind::BIN: return "BIN"; case TKind::INT: return "INT";
        case TKind::IDENT: return "IDENT"; case TKind::COMMA: return "COMMA";
        case TKind::LBRACK: return "LBRACK"; case TKind::RBRACK: return "RBRACK";
        case TKind::PLUS: return "PLUS"; case TKind::NEWLINE: return "NEWLINE";
        case TKind::STRING: return "STRING"; case TKind::EOF_TOK: return "EOF";
    }
    return "?";
}

static vector<Token> lexAll(const string& src){
    vector<Token> toks; size_t i=0; int line=1, col=1;
    while(i < src.size()){
        bool matched=false;
        for(const auto& rule: TOKEN_RULES){
            smatch m; auto begin = src.cbegin()+i; auto end = src.cend();
            if(regex_search(begin, end, m, rule.re, regex_constants::match_continuous)){
                string text = m.str(); matched=true;
                if(rule.kind == TKind::NEWLINE){
                    toks.push_back({TKind::NEWLINE, "\n", line, col});
                    int newlines = (int)count(text.begin(), text.end(), '\n');
                    line += newlines; col = 1;
                }else if(rule.kind==TKind::WS || rule.kind==TKind::COMMENT){
                    col += (int)text.size();
                }else{
                    toks.push_back({rule.kind, text, line, col});
                    col += (int)text.size();
                }
                i += text.size();
                break;
            }
        }
        if(!matched){ ostringstream oss; oss << "Unknown token at " << line << ":" << col; throw runtime_error(oss.str()); }
    }
    toks.push_back({TKind::EOF_TOK, "", line, col});
    return toks;
}

// -----------------------------
// ISA & IR
// -----------------------------
enum class Op : uint8_t {
    LDI=1, MOV=2, ADD=3, SUB=4, AND=5, OR=6, XOR=7,
    LW=8, SW=9, JMP=10, BEQ=11, BNE=12, CALL=13, RET=14, HALT=15
};

static unordered_map<string, Op> MNEMONIC = {
    {"ldi", Op::LDI}, {"mov", Op::MOV}, {"add", Op::ADD}, {"sub", Op::SUB},
    {"and", Op::AND}, {"or", Op::OR},   {"xor", Op::XOR}, {"lw", Op::LW},
    {"sw", Op::SW},   {"jmp", Op::JMP}, {"beq", Op::BEQ}, {"bne", Op::BNE},
    {"call", Op::CALL},{"ret", Op::RET}, {"halt", Op::HALT}
};

struct Instr {
    Op op; uint8_t rd=0, rs1=0, rs2=0; int32_t imm=0; optional<string> label_ref; int src_line=0;
};

// -----------------------------
// Simple Macro Processor
// -----------------------------
struct Macro { string name; int arity; vector<string> body; };

static string trim(const string& s){
    size_t a = s.find_first_not_of(" \t\r\n"); if(a==string::npos) return ""; size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a,b-a+1);
}

static vector<string> splitCSV(const string& s){
    vector<string> out; string cur; int paren=0; for(char c: s){
        if(c==',' && paren==0){ out.push_back(trim(cur)); cur.clear(); }
        else { if(c=='[') paren++; if(c==']') paren--; cur.push_back(c);} }
    if(!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

static string macroExpand(const string& src){
    istringstream iss(src); string line; vector<Macro> macros; vector<string> out;
    bool inMacro=false; Macro cur;
    while(getline(iss, line)){
        string s = trim(line);
        if(!inMacro && s.rfind(".macro",0)==0){
            // .macro NAME N
            istringstream ls(s.substr(6)); string name; int arity=0; ls>>name>>arity; if(name.empty()) throw runtime_error(".macro missing name");
            inMacro=true; cur = Macro{name, arity, {}}; continue;
        }
        if(inMacro && s.rfind(".endm",0)==0){ macros.push_back(cur); inMacro=false; continue; }
        if(inMacro){ cur.body.push_back(line); continue; }

        // attempt expansion: token at start equals macro name
        bool expanded=false;
        for(const auto& m: macros){
            // match name at start and arguments
            string prefix = m.name + " "; string prefix2 = m.name + "\t"; string only = m.name;
            if(s==only || s.rfind(prefix,0)==0 || s.rfind(prefix2,0)==0){
                string argsPart = ""; if(s.size()>m.name.size()) argsPart = trim(s.substr(m.name.size()));
                vector<string> args = argsPart.empty()? vector<string>{} : splitCSV(argsPart);
                if((int)args.size()!=m.arity) throw runtime_error("Macro "+m.name+" expects "+to_string(m.arity)+" args");
                for(string bodyLine: m.body){
                    // positional substitution $1..$N
                    for(int i=0;i<m.arity;i++){ string key = "$"+to_string(i+1); size_t pos=0; while((pos = bodyLine.find(key, pos))!=string::npos){ bodyLine.replace(pos, key.size(), args[i]); pos += args[i].size(); } }
                    out.push_back(bodyLine);
                }
                expanded=true; break;
            }
        }
        if(!expanded) out.push_back(line);
    }
    if(inMacro) throw runtime_error("Unterminated .macro");
    // rebuild
    ostringstream oss; for(size_t i=0;i<out.size();++i){ oss<<out[i]; if(i+1<out.size()) oss<<"\n"; }
    return oss.str();
}

// -----------------------------
// Parser (pass 1)
// -----------------------------

enum class Section : uint16_t { UNDEF=0, TEXT=1, DATA=2 };

struct Sym { string name; Section sec; uint32_t value; bool global=false; };
struct Reloc { Section sec; uint32_t offset; uint16_t type; string name; }; // type 0 = rel32

class Parser {
public:
    explicit Parser(const vector<Token>& toks): toks(toks) {}

    struct Result {
        vector<Instr> instrs; vector<uint8_t> data; vector<Sym> symbols; vector<Reloc> relocs;
    };

    Result parse(){
        current = Section::TEXT;
        while(!at(TKind::EOF_TOK)){
            if(at(TKind::NEWLINE)){ eat(TKind::NEWLINE); continue; }
            if(at(TKind::LABEL)){
                auto t = eat(TKind::LABEL);
                string name = t.value.substr(0, t.value.size()-1);
                uint32_t ofs = (current==Section::TEXT)? (uint32_t)instrs.size()*8u : (uint32_t)data.size();
                defineSymbol(name, current, ofs);
                maybe(TKind::NEWLINE); continue;
            }
            if(at(TKind::DIRECTIVE)){
                handleDirective(); continue;
            }
            if(current==Section::TEXT && at(TKind::IDENT)){
                parseInstr(); maybe(TKind::NEWLINE); continue;
            }
            // otherwise skip token to avoid infinite loop
            i++;
        }

        // Ensure pending globals that weren't defined become undefined symbols
        for(const auto &name : pendingGlobals){
            if(symIndex.count(name)) continue; // defensive
            Sym s{name, Section::UNDEF, 0u, true};
            symIndex[name] = symbols.size();
            symbols.push_back(s);
        }
        pendingGlobals.clear();

        Result r; r.instrs=move(instrs); r.data=move(data); r.symbols=move(symbols); r.relocs=move(relocs); return r;
    }

private:
    const vector<Token>& toks; size_t i = 0; Section current = Section::TEXT;
    vector<Instr> instrs; vector<uint8_t> data; vector<Sym> symbols; unordered_map<string,size_t> symIndex;
    vector<Reloc> relocs; unordered_set<string> pendingGlobals;

    bool at(TKind k) const { return toks[i].kind == k; }
    const Token& eat(TKind k){ const Token& t = toks[i]; if(t.kind != k){ ostringstream oss; oss << "Expected " << kindName(k) << ", got " << kindName(t.kind) << " at " << t.line << ":" << t.col; throw runtime_error(oss.str()); } i++; return t; }
    bool maybe(TKind k){ if(at(k)){ eat(k); return true; } return false; }

    uint8_t parseReg(){ const Token& t = eat(TKind::REGISTER); int n = stoi(t.value.substr(1)); if(n < 0 || n > 31) throw runtime_error("register out of range (0-31)"); return (uint8_t)n; }
    int32_t parseInt(){ const Token& t = toks[i]; if(t.kind == TKind::HEX){ i++; return (int32_t)stol(t.value, nullptr, 16); } if(t.kind == TKind::BIN){ i++; return (int32_t)stol(t.value.substr(2), nullptr, 2); } if(t.kind == TKind::INT){ i++; return (int32_t)stol(t.value, nullptr, 10); } ostringstream oss; oss << "Expected int at " << t.line << ":" << t.col; throw runtime_error(oss.str()); }

    pair<int32_t, optional<string>> parseLabelRef(){ const Token& t = toks[i]; if(t.kind == TKind::IDENT){ i++; return {0, t.value}; } return {parseInt(), nullopt}; }
    void expectComma(){ if(!maybe(TKind::COMMA)) throw runtime_error("Expected ', '"); }

    void defineSymbol(const string& name, Section sec, uint32_t value){
        if(symIndex.count(name)) throw runtime_error("Duplicate symbol: "+name);
        Sym s{name, sec, value, false};
        auto it = pendingGlobals.find(name); if(it!=pendingGlobals.end()){ s.global=true; pendingGlobals.erase(it); }
        symIndex[name] = symbols.size(); symbols.push_back(s);
    }

    void markGlobal(const string& name){
        if(symIndex.count(name)) symbols[symIndex[name]].global = true; else pendingGlobals.insert(name);
    }

    void handleDirective(){
        string d = eat(TKind::DIRECTIVE).value; // includes dot
        string low = d; transform(low.begin(), low.end(), low.begin(), ::tolower);
        if(low==".text"){ current = Section::TEXT; maybe(TKind::NEWLINE); return; }
        if(low==".data"){ current = Section::DATA; maybe(TKind::NEWLINE); return; }
        if(low==".global"){
            // .global sym1, sym2, ... (until newline)
            while(!at(TKind::NEWLINE) && !at(TKind::EOF_TOK)){
                if(at(TKind::IDENT)) { string n = eat(TKind::IDENT).value; markGlobal(n); }
                else if(at(TKind::COMMA)) { eat(TKind::COMMA); }
                else { break; }
            }
            maybe(TKind::NEWLINE); return;
        }
        // data emission directives (valid only in .data)
        if(current!=Section::DATA){
            // skip rest of line for unknown/unsupported directives when not in .data
            while(!at(TKind::NEWLINE) && !at(TKind::EOF_TOK)) i++; maybe(TKind::NEWLINE); return;
        }
        if(low==".byte"){
            // .byte v1, v2, ...
            do {
                if(at(TKind::IDENT)){
                    // reloc to symbol (store 1 byte later not supported) -> we support only numeric here
                    auto [v,lbl]=parseLabelRef();
                    if(lbl) throw runtime_error(".byte does not support relocations; use .word for labels");
                    int32_t v32 = v; data.push_back((uint8_t)(v32 & 0xFF));
                }else{
                    int32_t v = parseInt(); data.push_back((uint8_t)(v & 0xFF));
                }
            } while(maybe(TKind::COMMA));
            maybe(TKind::NEWLINE); return;
        }
        if(low==".word"){
            // .word value (32-bit LE) ; supports reloc to symbol
            if(at(TKind::IDENT)){
                // label -> placeholder + relocation
                auto [v,lbl] = parseLabelRef(); // we expect lbl for IDENT case
                if(lbl){
                    uint32_t ofs = (uint32_t)data.size();
                    // placeholder 4 bytes
                    data.push_back(0); data.push_back(0); data.push_back(0); data.push_back(0);
                    relocs.push_back({Section::DATA, ofs, 0, *lbl});
                } else {
                    // defensive fallback (should not happen)
                    uint32_t u = (uint32_t)v;
                    data.push_back((uint8_t)(u & 0xFF));
                    data.push_back((uint8_t)((u>>8)&0xFF));
                    data.push_back((uint8_t)((u>>16)&0xFF));
                    data.push_back((uint8_t)((u>>24)&0xFF));
                }
            }else{
                // immediate numeric
                int32_t v = parseInt();
                uint32_t u=(uint32_t)v;
                data.push_back((uint8_t)(u & 0xFF));
                data.push_back((uint8_t)((u>>8)&0xFF));
                data.push_back((uint8_t)((u>>16)&0xFF));
                data.push_back((uint8_t)((u>>24)&0xFF));
            }
            maybe(TKind::NEWLINE); return;
        }
        // unknown directive inside .data -> skip rest of line
        while(!at(TKind::NEWLINE) && !at(TKind::EOF_TOK)) i++; maybe(TKind::NEWLINE);
    }

    void parseInstr(){
        string mnem = eat(TKind::IDENT).value; transform(mnem.begin(), mnem.end(), mnem.begin(), ::tolower);
        if(!MNEMONIC.count(mnem)) throw runtime_error("Unknown mnemonic: " + mnem);
        Op op = MNEMONIC[mnem]; int line = toks[i-1].line;
        uint8_t rd=0, rs1=0, rs2=0; int32_t imm=0; optional<string> lbl;
        switch(op){
            case Op::LDI: rd = parseReg(); expectComma(); imm = parseInt(); break;
            case Op::MOV: rd = parseReg(); expectComma(); rs1 = parseReg(); break;
            case Op::ADD: case Op::SUB: case Op::AND: case Op::OR: case Op::XOR:
                rd = parseReg(); expectComma(); rs1 = parseReg(); expectComma(); rs2 = parseReg(); break;
            case Op::LW:
                rd = parseReg(); expectComma(); eat(TKind::LBRACK); rs1 = parseReg(); imm = 0; eat(TKind::RBRACK); break;
            case Op::SW:
                rs2 = parseReg(); expectComma(); eat(TKind::LBRACK); rs1 = parseReg(); imm = 0; eat(TKind::RBRACK); break;
            case Op::JMP: case Op::CALL: { auto [v, name] = parseLabelRef(); imm = v; lbl = name; break; }
            case Op::BEQ: case Op::BNE: { rs1 = parseReg(); expectComma(); rs2 = parseReg(); expectComma(); auto [v, name] = parseLabelRef(); imm = v; lbl = name; break; }
            case Op::RET: case Op::HALT: break;
        }
        size_t idx = instrs.size(); instrs.push_back({op, rd, rs1, rs2, imm, lbl, line});
        if(lbl.has_value()){
            // reloc points at imm field inside instruction (byte offset +4)
            relocs.push_back({Section::TEXT, (uint32_t)(idx*8 + 4), 0, *lbl});
        }
    }
};

// -----------------------------
// Binary helpers
// -----------------------------
static void write_u8(vector<uint8_t>& buf, uint8_t v){ buf.push_back(v); }
static void write_u16(vector<uint8_t>& buf, uint16_t v){ buf.push_back((uint8_t)(v & 0xFF)); buf.push_back((uint8_t)((v >> 8) & 0xFF)); }
static void write_u32(vector<uint8_t>& buf, uint32_t v){ buf.push_back((uint8_t)(v & 0xFF)); buf.push_back((uint8_t)((v >> 8) & 0xFF)); buf.push_back((uint8_t)((v >> 16) & 0xFF)); buf.push_back((uint8_t)((v >> 24) & 0xFF)); }
static void write_i32(vector<uint8_t>& buf, int32_t v){ write_u32(buf, (uint32_t)v); }

// -----------------------------
// Object File Writer (.vmo)
// -----------------------------
static const uint32_t MAGIC = 0x564D4F46; // 'VMOF'
static const uint16_t VERSION = 2;         // bumped for Module 3 format

// Header layout (little endian):
// u32 MAGIC, u16 VERSION, u16 flags
// u32 text_off, u32 text_size
// u32 data_off, u32 data_size
// u32 sym_off,  u32 sym_count
// u32 rel_off,  u32 rel_count

static vector<uint8_t> assemble(const string& raw){
    // 1) macros
    string src = macroExpand(raw);
    // 2) lex & parse
    auto toks = lexAll(src); Parser p(toks); auto res = p.parse();

    // 3) encode .text
    vector<uint8_t> text; text.reserve(res.instrs.size()*8);
    for(const auto& inst : res.instrs){
        write_u8(text, static_cast<uint8_t>(inst.op));
        write_u8(text, inst.rd); write_u8(text, inst.rs1); write_u8(text, inst.rs2);
        write_i32(text, inst.imm);
    }
    vector<uint8_t>& data = res.data;

    // Compute layout
    vector<uint8_t> out; out.reserve(128 + text.size() + data.size());
    const uint32_t header_size = 4+2+2 + 4+4 + 4+4 + 4+4 + 4+4; // 40 bytes
    uint32_t text_off = header_size;
    uint32_t data_off = text_off + (uint32_t)text.size();

    // Build symbol table blob
    vector<uint8_t> symblob;
    auto write_sym = [&](const Sym& s){
        write_u16(symblob, (uint16_t)s.sec); // section
        write_u16(symblob, (uint16_t)(s.global?1:0)); // flags (bit0 global)
        write_u32(symblob, s.value);
        write_u16(symblob, (uint16_t)s.name.size());
        symblob.insert(symblob.end(), s.name.begin(), s.name.end());
    };
    for(const auto& s : res.symbols) write_sym(s);

    uint32_t sym_off = data_off + (uint32_t)data.size();

    // Build relocation table blob
    vector<uint8_t> relblob;
    auto write_rel = [&](const Reloc& r){
        write_u16(relblob, (uint16_t)r.sec); // section
        write_u16(relblob, r.type);          // type (0=rel32)
        write_u32(relblob, r.offset);        // offset within section
        write_u16(relblob, (uint16_t)r.name.size());
        relblob.insert(relblob.end(), r.name.begin(), r.name.end());
    };
    for(const auto& r : res.relocs) write_rel(r);

    uint32_t rel_off = sym_off + (uint32_t)symblob.size();

    // 4) header
    write_u32(out, MAGIC); write_u16(out, VERSION); write_u16(out, 0);
    write_u32(out, text_off); write_u32(out, (uint32_t)text.size());
    write_u32(out, data_off); write_u32(out, (uint32_t)data.size());
    write_u32(out, sym_off);  write_u32(out, (uint32_t)res.symbols.size());
    write_u32(out, rel_off);  write_u32(out, (uint32_t)res.relocs.size());

    // 5) sections
    out.insert(out.end(), text.begin(), text.end());
    out.insert(out.end(), data.begin(), data.end());
    out.insert(out.end(), symblob.begin(), symblob.end());
    out.insert(out.end(), relblob.begin(), relblob.end());

    return out;
}

// -----------------------------
// Utilities
// -----------------------------
static string read_file(const string& path){ ifstream ifs(path, ios::binary); if(!ifs) throw runtime_error("Cannot open file: "+path); ostringstream ss; ss<<ifs.rdbuf(); return ss.str(); }
static void write_file(const string& path, const vector<uint8_t>& data){ ofstream ofs(path, ios::binary); if(!ofs) throw runtime_error("Cannot write file: "+path); ofs.write((const char*)data.data(), (streamsize)data.size()); }
static string hexdump(const vector<uint8_t>& b){ ostringstream out; for(size_t i=0;i<b.size();i+=16){ out << std::uppercase << std::hex << setw(8) << setfill('0') << i << "  " << std::nouppercase; for(size_t j=i;j<min(i+16, b.size());++j){ if(j==i) out << std::uppercase << std::hex << setfill('0'); out << setw(2) << std::uppercase << std::hex << (int)b[j] << std::nouppercase; if(j+1<min(i+16,b.size())) out << " "; } out << "\n"; } return out.str(); }

// -----------------------------
// CLI
// -----------------------------
static void usage(const char* prog){
    cerr << "Usage:\n"
         << "  " << prog << " assemble <input.vmasm> [-o output.vmo]\n"
         << "  " << prog << " dump <file.vmo>\n";
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false); cin.tie(nullptr);
    if(argc < 2){ usage(argv[0]); return 1; }
    string cmd = argv[1];
    try{
        if(cmd == "assemble"){
            if(argc < 3){ usage(argv[0]); return 1; }
            string in = argv[2]; string outPath;
            for(int i=3;i<argc;i++){
                string a = argv[i];
                if(a=="-o" || a=="--output"){ if(i+1 >= argc){ cerr << "Missing output path after " << a << "\n"; return 1; } outPath = argv[++i]; }
                else { cerr << "Unknown option: " << a << "\n"; return 1; }
            }
            string src = read_file(in); auto blob = assemble(src);
            if(outPath.empty()){ outPath = in; size_t dot = outPath.find_last_of('.'); if(dot != string::npos) outPath = outPath.substr(0, dot); outPath += ".vmo"; }
            write_file(outPath, blob); cout << "Wrote " << outPath << " (" << blob.size() << " bytes)\n"; return 0;
        }else if(cmd == "dump"){
            if(argc < 3){ usage(argv[0]); return 1; }
            string path = argv[2]; string data = read_file(path); vector<uint8_t> bytes(data.begin(), data.end()); cout << hexdump(bytes); return 0;
        }else{ usage(argv[0]); return 1; }
    }catch(const exception& e){ cerr << "Error: " << e.what() << "\n"; return 1; }
}
// End of vm_asm.cpp
