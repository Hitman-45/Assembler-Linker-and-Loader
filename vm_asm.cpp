// vmasm.cpp
// VM Assembler — Modules 1 & 2 (C++17)
// - Tokenize, parse assembly, generate 8-byte instructions
// - Emit .vmo object file with header + .text + symbol table + relocation table

#include <bits/stdc++.h>
using namespace std;

// -----------------------------
// Tokenizer
// -----------------------------
enum class TKind {
    WS, COMMENT, DIRECTIVE, LABEL, REGISTER, HEX, BIN, INT,
    IDENT, COMMA, LBRACK, RBRACK, PLUS, NEWLINE, EOF_TOK
};

struct Token {
    TKind kind;
    string value;
    int line;
    int col;
};

struct Rule {
    TKind kind;
    regex re;
};

static const vector<Rule> TOKEN_RULES = {
    {TKind::WS,        regex(R"([ \t]+)")},
    {TKind::COMMENT,   regex(R"(;.*)")},
    {TKind::DIRECTIVE, regex(R"(\.[A-Za-z_][A-Za-z0-9_]*)")},
    {TKind::LABEL,     regex(R"([A-Za-z_][A-Za-z0-9_]*:)")},
    {TKind::REGISTER,  regex(R"((?:r|x)(?:[12]?\d|3[01]|\d)\b)")},
    {TKind::HEX,       regex(R"(0x[0-9A-Fa-f]+)")},
    {TKind::BIN,       regex(R"(0b[01]+)")},
    {TKind::INT,       regex(R"(-?\d+)")},
    {TKind::IDENT,     regex(R"([A-Za-z_][A-Za-z0-9_]*)")},
    {TKind::COMMA,     regex(R"(,)")},
    {TKind::LBRACK,    regex(R"(\[)")},
    {TKind::RBRACK,    regex(R"(\])")},
    {TKind::PLUS,      regex(R"(\+)")},
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
        case TKind::EOF_TOK: return "EOF";
    }
    return "?";
}

static vector<Token> lexAll(const string& src){
    vector<Token> toks;
    size_t i=0;
    int line=1, col=1;

    while(i < src.size()){
        bool matched=false;
        for(const auto& rule: TOKEN_RULES){
            smatch m;
            auto begin = src.cbegin()+i;
            auto end   = src.cend();
            if(regex_search(begin, end, m, rule.re,
                            regex_constants::match_continuous)){
                string text = m.str();
                matched=true;
                if(rule.kind == TKind::NEWLINE){
                    toks.push_back({TKind::NEWLINE, "\n", line, col});
                    int newlines = (int)count(text.begin(), text.end(), '\n');
                    line += newlines; col = 1;
                }else if(rule.kind==TKind::WS || rule.kind==TKind::COMMENT){
                    // skip
                    col += (int)text.size();
                }else{
                    toks.push_back({rule.kind, text, line, col});
                    col += (int)text.size();
                }
                i += text.size();
                break;
            }
        }
        if(!matched){
            ostringstream oss;
            oss << "Unknown token at " << line << ":" << col;
            throw runtime_error(oss.str());
        }
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
    Op op;
    uint8_t rd=0, rs1=0, rs2=0;
    int32_t imm=0;
    optional<string> label_ref;
    int src_line=0;
};

// -----------------------------
// Parser (pass 1)
// -----------------------------
class Parser {
public:
    explicit Parser(const vector<Token>& toks): toks(toks) {}

    tuple<vector<Instr>, unordered_map<string,int>, vector<pair<int,string>>> parse(){
        while(!at(TKind::EOF_TOK)){
            if(at(TKind::NEWLINE)){ eat(TKind::NEWLINE); continue; }
            if(at(TKind::LABEL)){
                auto t = eat(TKind::LABEL);
                string name = t.value.substr(0, t.value.size()-1);
                labels[name] = (int)instrs.size() * 8; // 8-byte inst
                maybe(TKind::NEWLINE);
                continue;
            }
            if(at(TKind::DIRECTIVE)){
                eat(TKind::DIRECTIVE);
                // skip rest of line
                while(!at(TKind::NEWLINE) && !at(TKind::EOF_TOK)) i++;
                maybe(TKind::NEWLINE);
                continue;
            }
            if(at(TKind::IDENT)){
                parseInstr();
                maybe(TKind::NEWLINE);
                continue;
            }
            // Fallback skip
            i++;
        }
        return {instrs, labels, relocs};
    }

private:
    const vector<Token>& toks;
    size_t i = 0;
    vector<Instr> instrs;
    unordered_map<string,int> labels;
    vector<pair<int,string>> relocs;

    bool at(TKind k) const { return toks[i].kind == k; }
    const Token& eat(TKind k){
        const Token& t = toks[i];
        if(t.kind != k){
            ostringstream oss;
            oss << "Expected " << kindName(k) << ", got " << kindName(t.kind)
                << " at " << t.line << ":" << t.col;
            throw runtime_error(oss.str());
        }
        i++;
        return t;
    }
    bool maybe(TKind k){
        if(at(k)){ eat(k); return true; }
        return false;
    }

    uint8_t parseReg(){
        const Token& t = eat(TKind::REGISTER);
        // t.value like r0 / x12 etc -> strip leading letter
        int n = stoi(t.value.substr(1));
        if(n < 0 || n > 255) throw runtime_error("register out of range");
        return (uint8_t)n;
    }

    int32_t parseInt(){
        const Token& t = toks[i];
        if(t.kind == TKind::HEX){ i++; return (int32_t)stol(t.value, nullptr, 16); }
        if(t.kind == TKind::BIN){ i++; return (int32_t)stol(t.value.substr(2), nullptr, 2); }
        if(t.kind == TKind::INT){ i++; return (int32_t)stol(t.value, nullptr, 10); }
        ostringstream oss; oss << "Expected int at " << t.line << ":" << t.col;
        throw runtime_error(oss.str());
    }

    void expectComma(){
        if(!maybe(TKind::COMMA)) throw runtime_error("Expected ','");
    }

    pair<int32_t, optional<string>> parseLabelRef(){
        const Token& t = toks[i];
        if(t.kind == TKind::IDENT){ i++; return {0, t.value}; }
        return {parseInt(), nullopt};
    }

    void parseInstr(){
        string mnem = eat(TKind::IDENT).value;
        // lowercase
        transform(mnem.begin(), mnem.end(), mnem.begin(), ::tolower);
        if(!MNEMONIC.count(mnem)) throw runtime_error("Unknown mnemonic: " + mnem);
        Op op = MNEMONIC[mnem];
        int line = toks[i-1].line;

        uint8_t rd=0, rs1=0, rs2=0;
        int32_t imm=0;
        optional<string> lbl;

        switch(op){
            case Op::LDI:
                rd = parseReg(); expectComma(); imm = parseInt(); break;
            case Op::MOV:
                rd = parseReg(); expectComma(); rs1 = parseReg(); break;
            case Op::ADD: case Op::SUB: case Op::AND: case Op::OR: case Op::XOR:
                rd = parseReg(); expectComma(); rs1 = parseReg(); expectComma(); rs2 = parseReg(); break;
            case Op::LW:
                rd = parseReg(); expectComma(); eat(TKind::LBRACK); rs1 = parseReg(); imm = 0; eat(TKind::RBRACK); break;
            case Op::SW:
                rs2 = parseReg(); expectComma(); eat(TKind::LBRACK); rs1 = parseReg(); imm = 0; eat(TKind::RBRACK); break;
            case Op::JMP: case Op::CALL: {
                auto [v, name] = parseLabelRef();
                imm = v; lbl = name; break;
            }
            case Op::BEQ: case Op::BNE: {
                rs1 = parseReg(); expectComma(); rs2 = parseReg(); expectComma();
                auto [v, name] = parseLabelRef();
                imm = v; lbl = name; break;
            }
            case Op::RET: case Op::HALT:
                break;
        }

        size_t idx = instrs.size();
        instrs.push_back({op, rd, rs1, rs2, imm, lbl, line});
        if(lbl.has_value()){
            relocs.emplace_back((int)idx*8, *lbl);
        }
    }
};

// -----------------------------
// Binary helpers
// -----------------------------
static void write_u8(vector<uint8_t>& buf, uint8_t v){ buf.push_back(v); }
static void write_u16(vector<uint8_t>& buf, uint16_t v){
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
}
static void write_u32(vector<uint8_t>& buf, uint32_t v){
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)((v >> 16) & 0xFF));
    buf.push_back((uint8_t)((v >> 24) & 0xFF));
}
static void write_i32(vector<uint8_t>& buf, int32_t v){
    write_u32(buf, (uint32_t)v);
}

// -----------------------------
// Object File Writer (.vmo)
// -----------------------------
static const uint32_t MAGIC = 0x564D4F46; // 'VMOF'
static const uint16_t VERSION = 1;

static vector<uint8_t> assemble(const string& src){
    auto toks = lexAll(src);
    Parser p(toks);
    vector<Instr> instrs;
    unordered_map<string,int> labels;
    vector<pair<int,string>> relocs;
    tie(instrs,labels,relocs) = p.parse();

    vector<uint8_t> code;
    code.reserve(instrs.size()*8);
    for(const auto& inst : instrs){
        write_u8(code, static_cast<uint8_t>(inst.op));
        write_u8(code, inst.rd);
        write_u8(code, inst.rs1);
        write_u8(code, inst.rs2);
        write_i32(code, inst.imm);
    }

    const uint32_t text_off = 16;
    vector<uint8_t> out;
    out.reserve(16 + code.size() + labels.size()*12 + relocs.size()*12);

    // Header: <IHHII
    write_u32(out, MAGIC);
    write_u16(out, VERSION);
    write_u16(out, 0);            // flags
    write_u32(out, text_off);
    write_u32(out, (uint32_t)code.size());

    // .text
    out.insert(out.end(), code.begin(), code.end());

    // Symbol table: [u32 offset][u16 name_len][name bytes]
    for(const auto& kv : labels){
        const string& name = kv.first;
        uint32_t ofs = (uint32_t)kv.second;
        write_u32(out, ofs);
        write_u16(out, (uint16_t)name.size());
        out.insert(out.end(), name.begin(), name.end());
    }

    // Relocation table: [u32 offset][u16 type=0][u16 name_len][name bytes]
    for(const auto& pr : relocs){
        uint32_t ofs = (uint32_t)pr.first;
        const string& name = pr.second;
        write_u32(out, ofs);
        write_u16(out, 0); // type 0 = rel32
        write_u16(out, (uint16_t)name.size());
        out.insert(out.end(), name.begin(), name.end());
    }

    return out;
}

// -----------------------------
// Utilities
// -----------------------------
static string read_file(const string& path){
    ifstream ifs(path, ios::binary);
    if(!ifs) throw runtime_error("Cannot open file: " + path);
    ostringstream ss; ss << ifs.rdbuf();
    return ss.str();
}

static void write_file(const string& path, const vector<uint8_t>& data){
    ofstream ofs(path, ios::binary);
    if(!ofs) throw runtime_error("Cannot write file: " + path);
    ofs.write((const char*)data.data(), (streamsize)data.size());
}

static string hexdump(const vector<uint8_t>& b){
    ostringstream out;
    for(size_t i=0;i<b.size();i+=16){
        out << std::uppercase << std::hex << setw(8) << setfill('0') << i << "  " << std::nouppercase;
        for(size_t j=i;j<min(i+16, b.size());++j){
            if(j==i) out << std::uppercase << std::hex << setfill('0');
            out << setw(2) << std::uppercase << std::hex << (int)b[j] << std::nouppercase;
            if(j+1<min(i+16,b.size())) out << " ";
        }
        out << "\n";
    }
    return out.str();
}

// -----------------------------
// CLI
// -----------------------------
static void usage(const char* prog){
    cerr << "Usage:\n"
         << "  " << prog << " assemble <input.vmasm> [-o output.vmo]\n"
         << "  " << prog << " dump <file.vmo>\n";
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    if(argc < 2){ usage(argv[0]); return 1; }
    string cmd = argv[1];

    try{
        if(cmd == "assemble"){
            if(argc < 3){ usage(argv[0]); return 1; }
            string in = argv[2];
            string outPath;

            // parse optional -o
            for(int i=3;i<argc;i++){
                string a = argv[i];
                if(a=="-o" || a=="--output"){
                    if(i+1 >= argc){ cerr << "Missing output path after " << a << "\n"; return 1; }
                    outPath = argv[++i];
                }else{
                    cerr << "Unknown option: " << a << "\n"; return 1;
                }
            }

            string src = read_file(in);
            auto blob = assemble(src);

            if(outPath.empty()){
                // replace extension with .vmo
                outPath = in;
                size_t dot = outPath.find_last_of('.');
                if(dot != string::npos) outPath = outPath.substr(0, dot);
                outPath += ".vmo";
            }

            write_file(outPath, blob);
            cout << "Wrote " << outPath << " (" << blob.size() << " bytes)\n";
            return 0;
        }else if(cmd == "dump"){
            if(argc < 3){ usage(argv[0]); return 1; }
            string path = argv[2];
            string data = read_file(path);
            vector<uint8_t> bytes(data.begin(), data.end());
            cout << hexdump(bytes);
            return 0;
        }else{
            usage(argv[0]); return 1;
        }
    }catch(const exception& e){
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
