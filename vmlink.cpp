// vmlink.cpp
// Simple linker for .vmo -> .vmc
// g++ -std=c++17 vmlink.cpp -o vmlink

#include <bits/stdc++.h>
using namespace std;

static uint32_t read_u32(const vector<uint8_t>& b, size_t off){
    if(off+4 > b.size()) throw runtime_error("read_u32 out of range");
    return uint32_t(b[off]) | (uint32_t(b[off+1])<<8) | (uint32_t(b[off+2])<<16) | (uint32_t(b[off+3])<<24);
}
static uint16_t read_u16(const vector<uint8_t>& b, size_t off){
    if(off+2 > b.size()) throw runtime_error("read_u16 out of range");
    return uint16_t(b[off]) | (uint16_t(b[off+1])<<8);
}
static void write_u32(vector<uint8_t>& b, uint32_t v){ b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)((v>>8)&0xFF)); b.push_back((uint8_t)((v>>16)&0xFF)); b.push_back((uint8_t)((v>>24)&0xFF)); }
static void write_u16(vector<uint8_t>& b, uint16_t v){ b.push_back((uint8_t)(v & 0xFF)); b.push_back((uint8_t)((v>>8)&0xFF)); }

enum class Section : uint16_t { UNDEF = 0, TEXT = 1, DATA = 2 };

// Symbol record in object file
struct SymRec {
    Section sec;
    uint16_t flags;
    uint32_t value; // offset within section
    string name;
    // derived:
    size_t object_index; // which object file
};

// Relocation record in object file
struct RelRec {
    Section sec;
    uint16_t type;
    uint32_t offset; // within section
    string name;     // symbol name
    size_t object_index;
};

// Simple object file representation
struct ObjFile {
    string path;
    vector<uint8_t> raw;         // entire file bytes
    vector<uint8_t> text;
    vector<uint8_t> data;
    vector<SymRec> symbols;
    vector<RelRec> relocs;
};

static void usage(const char* prog){
    cerr << "Usage:\n  " << prog << " -o output.vmc input1.vmo input2.vmo ...\n";
}

// parse .vmo according to vmasm format described in project
static ObjFile parse_vmo(const string& path){
    ifstream ifs(path, ios::binary);
    if(!ifs) throw runtime_error("Cannot open " + path);
    vector<uint8_t> buf((istreambuf_iterator<char>(ifs)), istreambuf_iterator<char>());
    if(buf.size() < 40) throw runtime_error("File too small: " + path);

    const uint32_t MAGIC = 0x564D4F46; // 'VMOF'
    uint32_t magic = read_u32(buf, 0);
    if(magic != MAGIC) throw runtime_error("Bad magic: " + path);
    uint16_t ver = read_u16(buf, 4);
    (void)ver;
    // skip flags (u16) at 6
    uint32_t text_off = read_u32(buf, 8);
    uint32_t text_size = read_u32(buf, 12);
    uint32_t data_off = read_u32(buf, 16);
    uint32_t data_size = read_u32(buf, 20);
    uint32_t sym_off  = read_u32(buf, 24);
    uint32_t sym_count= read_u32(buf, 28);
    uint32_t rel_off  = read_u32(buf, 32);
    uint32_t rel_count= read_u32(buf, 36);

    ObjFile obj; obj.path = path; obj.raw = move(buf);

    // extract text/data
    if(text_off + text_size > obj.raw.size()) throw runtime_error("text section out of range: " + path);
    if(data_off + data_size > obj.raw.size()) throw runtime_error("data section out of range: " + path);
    obj.text.assign(obj.raw.begin()+text_off, obj.raw.begin()+text_off+text_size);
    obj.data.assign(obj.raw.begin()+data_off, obj.raw.begin()+data_off+data_size);

    // read symbol table
    size_t p = sym_off;
    for(uint32_t i=0;i<sym_count;i++){
        if(p + 2+2+4+2 > obj.raw.size()) throw runtime_error("symbol table truncated: " + path);
        Section sec = static_cast<Section>(read_u16(obj.raw, p)); p+=2;
        uint16_t flags = read_u16(obj.raw, p); p+=2;
        uint32_t val = read_u32(obj.raw, p); p+=4;
        uint16_t namelen = read_u16(obj.raw, p); p+=2;
        if(p + namelen > obj.raw.size()) throw runtime_error("symbol name truncated: " + path);
        string name((char*)(&obj.raw[p]), namelen); p += namelen;
        SymRec s{sec, flags, val, name, 0};
        obj.symbols.push_back(move(s));
    }

    // read relocation table
    p = rel_off;
    for(uint32_t i=0;i<rel_count;i++){
        if(p + 2+2+4+2 > obj.raw.size()) throw runtime_error("reloc table truncated: " + path);
        Section sec = static_cast<Section>(read_u16(obj.raw, p)); p+=2;
        uint16_t type = read_u16(obj.raw, p); p+=2;
        uint32_t offset = read_u32(obj.raw, p); p+=4;
        uint16_t namelen = read_u16(obj.raw, p); p+=2;
        if(p + namelen > obj.raw.size()) throw runtime_error("reloc name truncated: " + path);
        string name((char*)(&obj.raw[p]), namelen); p += namelen;
        RelRec r{sec, type, offset, name, 0};
        obj.relocs.push_back(move(r));
    }
    return obj;
}

int main(int argc, char** argv){
    ios::sync_with_stdio(false);
    if(argc < 3){ usage(argv[0]); return 1; }

    string outPath;
    vector<string> inputs;
    for(int i=1;i<argc;i++){
        string a = argv[i];
        if(a=="-o" || a=="--output"){ if(i+1>=argc){ cerr<<"Missing output path\n"; return 1; } outPath = argv[++i]; }
        else inputs.push_back(a);
    }
    if(outPath.empty()){ cerr<<"Output not specified (-o)\n"; return 1; }
    if(inputs.empty()){ cerr<<"No input objects\n"; return 1; }

    try{
        // load all objects
        vector<ObjFile> objs;
        objs.reserve(inputs.size());
        for(size_t i=0;i<inputs.size();++i){
            ObjFile obj = parse_vmo(inputs[i]);
            // mark index on sym/rel records
            for(auto &s : obj.symbols) s.object_index = objs.size();
            for(auto &r : obj.relocs) r.object_index = objs.size();
            objs.push_back(move(obj));
        }

        // assign base addresses for each object's sections in final layout
        // layout: text blocks concatenated, then data blocks concatenated
        uint32_t text_base = 0;
        vector<uint32_t> obj_text_base(objs.size(), 0), obj_data_base(objs.size(), 0);
        for(size_t i=0;i<objs.size();++i){
            obj_text_base[i] = text_base;
            text_base += (uint32_t)objs[i].text.size();
        }
        uint32_t data_base = text_base; // data follows text
        for(size_t i=0;i<objs.size();++i){
            obj_data_base[i] = data_base;
            data_base += (uint32_t)objs[i].data.size();
        }

        // Build global symbol table: name -> (Section, absolute address, defining object index, flags)
        struct GlobalSym { Section sec; uint32_t addr; uint16_t flags; size_t def_obj; };
        unordered_map<string, GlobalSym> gsym;

        // first pass: register all defined symbols
        for(size_t oi=0; oi<objs.size(); ++oi){
            for(const auto &s : objs[oi].symbols){
                if(s.sec == Section::UNDEF) continue; // skip undefined
                uint32_t absaddr = (s.sec == Section::TEXT) ? (obj_text_base[oi] + s.value) : (obj_data_base[oi] + s.value);
                if(gsym.count(s.name)){
                    // duplicate definition: error if both are non-weak (we don't have weak; check global flag)
                    auto &old = gsym[s.name];
                    bool old_is_global = (old.flags & 1);
                    bool new_is_global = (s.flags & 1);
                    // Duplicate definitions are always an error in this simple linker
                    ostringstream oss; oss << "Duplicate symbol: " << s.name << " defined in " << inputs[old.def_obj] << " and " << inputs[oi]; 
                    throw runtime_error(oss.str());
                }else{
                    GlobalSym g{ s.sec, absaddr, s.flags, oi };
                    gsym[s.name] = g;
                }
            }
        }

        // second pass: ensure undefined globals from objects are handled
        // We collect referenced names from relocations and undefined symbol table entries
        unordered_set<string> referenced;
        for(size_t oi=0; oi<objs.size(); ++oi){
            for(const auto &r : objs[oi].relocs) referenced.insert(r.name);
            for(const auto &s : objs[oi].symbols) if(s.sec == Section::UNDEF) referenced.insert(s.name);
        }

        // check for undefined references that are not in gsym
        vector<string> undef_list;
        for(const auto &name : referenced){
            if(!gsym.count(name)) undef_list.push_back(name);
        }
        if(!undef_list.empty()){
            ostringstream oss; oss << "Undefined symbols:";
            for(auto &n : undef_list) oss << " " << n;
            throw runtime_error(oss.str());
        }

        // Merge sections into final buffers
        vector<uint8_t> final_text; final_text.reserve(text_base);
        vector<uint8_t> final_data; final_data.reserve(data_base - text_base);
        for(size_t i=0;i<objs.size();++i){
            final_text.insert(final_text.end(), objs[i].text.begin(), objs[i].text.end());
        }
        for(size_t i=0;i<objs.size();++i){
            final_data.insert(final_data.end(), objs[i].data.begin(), objs[i].data.end());
        }

        // Apply relocations
        // For each relocation, find symbol absolute address and write into target at (section_base + offset) as 32-bit little endian
        for(size_t oi=0; oi<objs.size(); ++oi){
            for(const auto &r : objs[oi].relocs){
                // locate symbol
                auto it = gsym.find(r.name);
                if(it == gsym.end()) {
                    ostringstream os; os << "Relocation refers to undefined symbol: " << r.name; throw runtime_error(os.str());
                }
                GlobalSym gs = it->second;
                uint32_t symaddr = gs.addr;
                // target buffer and base
                vector<uint8_t>* buf = nullptr; uint32_t base = 0;
                if(r.sec == Section::TEXT){ buf = &final_text; base = obj_text_base[oi]; }
                else if(r.sec == Section::DATA){ buf = &final_data; base = obj_data_base[oi]; }
                else throw runtime_error("Unknown relocation section");
                uint32_t write_at = base + r.offset;
                // Ensure within bounds
                if(write_at + 4 > buf->size()){
                    ostringstream os; os << "Relocation write out of range in object " << inputs[oi] << " for symbol " << r.name; throw runtime_error(os.str());
                }
                if(r.type == 0){ // rel32 -> write absolute address (for simplicity)
                    uint32_t val = symaddr;
                    (*buf)[write_at + 0] = (uint8_t)(val & 0xFF);
                    (*buf)[write_at + 1] = (uint8_t)((val>>8)&0xFF);
                    (*buf)[write_at + 2] = (uint8_t)((val>>16)&0xFF);
                    (*buf)[write_at + 3] = (uint8_t)((val>>24)&0xFF);
                } else {
                    ostringstream os; os << "Unsupported reloc type " << r.type << " in object " << inputs[oi]; throw runtime_error(os.str());
                }
            }
        }

        // Build symbol table for executable (we include defined symbols)
        vector<pair<string, uint32_t>> exe_symbols;
        exe_symbols.reserve(gsym.size());
        for(const auto &kv : gsym){
            // only include defined (sec != UNDEF)
            if(kv.second.sec != Section::UNDEF) exe_symbols.push_back({kv.first, kv.second.addr});
        }

        // Choose entry point: symbol "main" if present, else 0
        uint32_t entry = 0;
        if(gsym.count("main")) entry = gsym["main"].addr;

        // Build .vmc file (we reuse header layout but change magic to 'VMCE' and zero reloc count)
        const uint32_t MAGIC_EXE = 0x564D4345; // 'VMCE'
        const uint16_t VERSION = 2;
        vector<uint8_t> out;
        // header size same as vmo header: 40 bytes
        const uint32_t header_size = 4+2+2 + 4+4 + 4+4 + 4+4 + 4+4;
        uint32_t text_off = header_size;
        uint32_t text_size = (uint32_t)final_text.size();
        uint32_t data_off = text_off + text_size;
        uint32_t data_size = (uint32_t)final_data.size();
        // build symbol blob
        vector<uint8_t> symblob;
        for(const auto &s : exe_symbols){
            // We'll store section as DATA if address >= data_base, else TEXT
            Section sec = (s.second >= data_base) ? Section::DATA : Section::TEXT;
            uint16_t flags = 1; // mark global in exe symbol table
            uint32_t value = s.second;
            write_u16(symblob, (uint16_t)sec);
            write_u16(symblob, flags);
            write_u32(symblob, value);
            write_u16(symblob, (uint16_t)s.first.size());
            symblob.insert(symblob.end(), s.first.begin(), s.first.end());
        }
        uint32_t sym_off = data_off + data_size;
        uint32_t sym_count = (uint32_t)exe_symbols.size();
        // no relocs in executable
        uint32_t rel_off = sym_off + (uint32_t)symblob.size();
        uint32_t rel_count = 0;

        // write header
        write_u32(out, MAGIC_EXE);
        write_u16(out, VERSION);
        write_u16(out, 0); // flags
        write_u32(out, text_off); write_u32(out, text_size);
        write_u32(out, data_off); write_u32(out, data_size);
        write_u32(out, sym_off);  write_u32(out, sym_count);
        write_u32(out, rel_off);  write_u32(out, rel_count);

        // sections
        out.insert(out.end(), final_text.begin(), final_text.end());
        out.insert(out.end(), final_data.begin(), final_data.end());
        out.insert(out.end(), symblob.begin(), symblob.end());
        // no reloc blob

        // Extra: append entry point (not in header) — instead we can store entry in a simple trailing metadata or require loader to use symbol 'main'.
        // For clarity, write a small 8-byte footer with entry address and magic "ENTR"
        // (Not required by spec, loader can examine symbol table; but adding entry is convenient)
        // Let's append "ENTR" + u32 entry
        const char footer_magic[4] = {'E','N','T','R'};
        out.insert(out.end(), footer_magic, footer_magic+4);
        write_u32(out, entry);

        // write file
        ofstream ofs(outPath, ios::binary);
        if(!ofs) throw runtime_error("Cannot write output file: " + outPath);
        ofs.write((const char*)out.data(), (streamsize)out.size());
        cout << "Wrote " << outPath << " (" << out.size() << " bytes). entry=" << entry << "\n";
        return 0;
    }catch(const exception& e){
        cerr << "Linker error: " << e.what() << "\n";
        return 1;
    }
}
