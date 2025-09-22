// vmloader.cpp
#include <bits/stdc++.h>
using namespace std;

struct VM {
    vector<uint8_t> memory;
    uint32_t PC = 0;

    VM(size_t memsize=65536) { memory.resize(memsize, 0); }

    void dump(size_t start, size_t len){
        for(size_t i=0;i<len;i++){
            if(i%16==0) cout << hex << setw(4) << setfill('0') << (start+i) << ": ";
            cout << hex << setw(2) << setfill('0') << (int)memory[start+i] << " ";
            if(i%16==15) cout << "\n";
        }
        cout << dec << "\n";
    }

    void trace(){
        cout << "[TRACE] PC=" << hex << PC 
             << " INSTR=" << setw(2) << (int)memory[PC] << "\n";
    }
};

// read little-endian helpers
static uint32_t read_u32(const vector<uint8_t>& b, size_t off){
    return (uint32_t)b[off] | ((uint32_t)b[off+1]<<8) | ((uint32_t)b[off+2]<<16) | ((uint32_t)b[off+3]<<24);
}

int main(int argc, char** argv){
    if(argc < 2){ cerr << "Usage: " << argv[0] << " program.vmc\n"; return 1; }

    // read vmc
    ifstream ifs(argv[1], ios::binary);
    if(!ifs) { cerr << "Cannot open " << argv[1] << "\n"; return 1; }
    vector<uint8_t> buf((istreambuf_iterator<char>(ifs)), {});

    // check magic
    uint32_t magic = read_u32(buf, 0);
    if(magic != 0x564D4345) { cerr << "Not a VMCE file\n"; return 1; }

    // parse header
    uint32_t text_off = read_u32(buf, 8);
    uint32_t text_size= read_u32(buf, 12);
    uint32_t data_off = read_u32(buf, 16);
    uint32_t data_size= read_u32(buf, 20);

    // create VM
    VM vm;
    copy(buf.begin()+text_off, buf.begin()+text_off+text_size, vm.memory.begin());
    copy(buf.begin()+data_off, buf.begin()+data_off+data_size, vm.memory.begin()+text_size);

    // parse footer (last 8 bytes: "ENTR" + entry)
    size_t foot = buf.size()-8;
    uint32_t entry = read_u32(buf, foot+4);
    vm.PC = entry;

    cout << "Loaded program. Entry=" << hex << entry << "\n";

    // debugging demo
    vm.dump(0, text_size + data_size);
    vm.trace();
}
