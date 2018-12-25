#ifndef included_value_h_
#define included_value_h_

#include <inttypes.h>
#include <vector>
#include <string>
#include <utilstrencodings.h>
#include <debugger/script.h>
#include <tinyformat.h>
#include <crypto/sha256.h>
#include <crypto/ripemd160.h>
#include <base58.h>
#include <bech32.h>

static bool VALUE_WARN = true;
static bool VALUE_EXTENDED = false; // 0bNNNN, etc

template<typename T1, typename T2>
inline void insert(T1& a, T2&& b) {
    a.insert(a.end(), b.begin(), b.end());
}

void DeserializeBool(const char* bv, std::vector<uint8_t>& output);

struct Value {
    enum {
        T_STRING,
        T_INT,
        T_DATA,
        T_OPCODE,
    } type;
    int64_t int64;
    opcodetype opcode;
    std::vector<uint8_t> data;
    std::string str;
    static std::vector<Value> parse_args(const std::vector<const char*> args) {
        std::vector<Value> result;
        for (auto& v : args) {
            size_t vlen = strlen(v);
            if (vlen > 0) {
                // brackets embed
                if (v[0] == '[' && v[vlen-1] == ']') {
                    result.emplace_back(parse_args(&v[1], vlen - 2));
                } else {
                    result.emplace_back(v, vlen/*, embedding*/);
                }
            }
        }
        return result;
    }
    static std::vector<Value> parse_args(const size_t argc, const char** argv, size_t argidx = 0) {
        std::vector<const char*> args;
        for (size_t i = argidx; i < argc; i++) args.push_back(argv[i]);
        return parse_args(args);
    }
    static std::vector<Value> parse_args(const char* args_string, size_t args_len = 0) {
        if (args_len == 0) args_len = strlen(args_string);
        std::vector<const char*> args;
        char* args_ptr[args_len];
        size_t arg_idx = 0;
        size_t start = 0;
        for (size_t i = 0; i <= args_len; i++) {
            char ch = args_string[i - (i == args_len)];
            if (ch == '[') {
                // start counting starting brackets, and stop when we hit depth 0
                size_t depth = 1;
                while ((++i) <= args_len && depth > 0) {
                    ch = args_string[i];
                    depth += (ch == '[') - (ch == ']');
                }
                if (depth > 0) {
                    fprintf(stderr, "parse error, unclosed [bracket (expected: ']') in \"%s\"\n", args_string);
                    exit(1);
                }
            }
            if (i == args_len || (ch == ']' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '#')) {
                if (start == i) {
                    start++;
                } else {
                    args_ptr[arg_idx] = strndup(&args_string[start], i - start);
                    args.push_back(args_ptr[arg_idx]);
                    arg_idx++;
                    start = i + 1;
                }
                if (ch == '#') {
                    // trim out remainder of this line
                    while (i < args_len && args_string[i] != '\n' && args_string[i] != '\r') {
                        i++;
                    }
                    start = i + 1;
                }
            }
        }
        std::vector<Value> result = parse_args(args);
        for (size_t i = 0; i < arg_idx; i++) free(args_ptr[i]);
        return result;
    }

    static std::string serialize(const std::vector<Value>& varr) {
        CScript s;
        for (const Value& v : varr) {
            v >> s;
        }
        return HexStr(s);
    }

    static Value from_secp256k1_pubkey(const void* secp256k1_pubkey_ptr);

    explicit Value(const int64_t i)                { int64 = i; type = T_INT; }
    explicit Value(const opcodetype o)             { opcode = o; type = T_OPCODE; }
    explicit Value(const std::vector<uint8_t>& d)  { data = d; type = T_DATA; }

    Value(const CScript& script) {
        data.clear();
        insert(data, script);
        type = T_DATA;
    }
    Value(std::vector<Value>&& v, bool fallthrough_single = false) {
        if (fallthrough_single && v.size() == 1) {
            type = v[0].type;
            data = v[0].data;
            str = v[0].str;
            int64 = v[0].int64;
            opcode = v[0].opcode;
            return;
        }
        type = T_DATA;
        data.clear();
        CScript s;
        for (auto& it : v) {
            it >> s;
        }
        insert(data, s);
    }
    Value(const char* v, size_t vlen = 0, bool non_numeric = false) {//, bool pushed = false, bool stack = false) {
        if (!v) { type = T_DATA; return; } // null value
        if (!vlen) vlen = strlen(v);
        if (vlen == 2 && v[0] == '0' && v[1] == 'x') {
            type = T_DATA;
            data.clear();
            return;
        }
        str = v;
        type = T_STRING;
        if (vlen > 1 && v[0] == '[' && v[vlen - 1] == ']') {
            CScript s;
            for (auto& it : parse_args(&v[1], vlen - 2)) {
                it >> s;
            }
            insert(data, s);
            type = T_DATA;
            return;
        }
        if (VALUE_EXTENDED && vlen > 1 && v[0] == '0' && v[1] == 'b') {
            type = T_DATA;
            DeserializeBool(&v[2], data);
            return;
        }
        int64 = non_numeric ? 0 : atoll(v);
        if (int64 != 0 || !strcmp(v, "0")) {
            // verify
            char buf[vlen + 1];
            sprintf(buf, "%" PRId64, int64);
            if (!strcmp(buf, v)) {
                // verified; can it be a hexstring too?
                if (!(vlen & 1)) {
                    std::vector<unsigned char> pushData(ParseHex(v));
                    if (pushData.size() == (vlen >> 1)) {
                        // it can; warn about using 0x for hex
                        if (VALUE_WARN) btc_logf("warning: ambiguous input %s is interpreted as a numeric value; use 0x%s to force into hexadecimal interpretation\n", v, v);
                    }
                }
                if (int64 >= 1 && int64 <= 16) {
                    if (VALUE_WARN) btc_logf("warning: ambiguous input %s is interpreted as a numeric value; use OP_%s to force into opcode\n", v, v);
                }
                type = T_INT;
                return;
            }
        }
        // opcode check
        opcode = GetOpCode(v);
        if (opcode != OP_INVALIDOPCODE) {
            type = T_OPCODE;
            return;
        }
        // hex string?
        if (!(vlen & 1)) {
            if (vlen > 2 && v[0] == '0' && v[1] == 'x') {
                vlen -= 2;
                v = &v[2];
            }
            data = ParseHex(v);
            if (data.size() == (vlen >> 1)) {
                type = T_DATA;
                return;
            }
        }
    }

    const Value& operator>>(CScript& s) const {
        switch (type) {
        case T_OPCODE:
            s << opcode;
            break;
        case T_INT:
            s << int64;
            break;
        case T_DATA:
            if (data.size() < 5) {
                // we need to push this as a number
                int64_t i = int_value();
                s << i;
                break;
            }
            // fall-through
        default:
            s << data_value();
        }
        return *this;
    }

    Value& operator+=(const Value& other) {
        data_value();
        insert(data, other.data_value());
        return *this;
    }
    bool operator==(const Value& other) const {
        return type == other.type &&
            (type == T_INT    ? int64 == other.int64 :
             type == T_STRING ? str == other.str :
             type == T_OPCODE ? opcode == other.opcode :
             data == other.data);
    }

    std::vector<uint8_t> data_value() const { return const_cast<Value*>(this)->data_value(); }

    std::vector<uint8_t> data_value(/*bool script = false*/) {
        switch (type) {
        case T_DATA:
            return data;
        case T_OPCODE:
            data.clear();
            insert(data, CScript() << opcode);
            return data;
        case T_INT:
            // use CScriptNum
            data = CScriptNum(int64).getvch();
            type = T_DATA;
            return data;
        default:
            // ascii representation
            data.resize(str.length());
            memcpy(data.data(), str.data(), str.length());
            return data;
        }
    }
    std::string& str_value() {
        switch (type) {
        case T_DATA: {
            str = HexStr(data);
            break;
        }
        case T_OPCODE:
            str = std::to_string(opcode);
            break;
        case T_INT:
            str = std::to_string(int64);
        case T_STRING:
            break;
        }
        type = T_STRING;
        return str;
    }
    std::string hex_str() const {
        switch (type) {
        case T_OPCODE:
            return strprintf("%02x", opcode);
        case T_INT:
            return HexStr(CScriptNum::serialize(int64));
        case T_DATA:
            return HexStr(data);
        default:
            fprintf(stderr, "cannot convert string into hex value: %s\n", str.c_str());
            return "";
        }
    }
    int64_t int_value() const {
        switch (type) {
        case T_INT:
            return int64;
        case T_OPCODE:
            return opcode;
        case T_DATA:
            return CScriptNum(data, false).getint64();
        default:
            fprintf(stderr, "cannot convert string into integer value: %s\n", str.c_str());
            return -1;
        }
    }
    void do_reverse() {
        std::vector<char> vc;
        int64_t j;
        switch (type) {
        case T_INT:
            for (int64_t z = int64; z; z = z / 10) {
                vc.push_back(z % 10);
            }
            j = 0;
            for (auto it = vc.rbegin(); it != vc.rend(); ++it) {
                j = (j * 10) + *it;
            }
            int64 = j;
            return;
        case T_DATA:
            std::reverse(std::begin(data), std::end(data));
            return;
        case T_STRING:
            std::reverse(str.begin(), str.end());
            return;
        default:
            fprintf(stderr, "irreversible value type\n");
            exit(1);
        }
    }
    void do_sha256() {
        data_value();
        type = T_DATA;
        CSHA256 s;
        s.Write(data.data(), data.size());
        data.resize(CSHA256::OUTPUT_SIZE);
        s.Finalize(data.data());
    }
    void do_ripemd160() {
        data_value();
        type = T_DATA;
        CRIPEMD160 s;
        s.Write(data.data(), data.size());
        data.resize(CRIPEMD160::OUTPUT_SIZE);
        s.Finalize(data.data());
    }
    void do_hash256() {
        do_sha256();
        do_sha256();
    }
    void do_hash160() {
        do_sha256();
        do_ripemd160();
    }
    void do_base58enc() {
        data_value();
        str = EncodeBase58(data);
        type = T_STRING;
    }
    void do_base58dec() {
        if (type != T_STRING) {
            fprintf(stderr, "cannot base58-decode non-string value\n");
            return;
        }
        if (!DecodeBase58(str, data)) {
            fprintf(stderr, "decode failed\n");
        }
        type = T_DATA;
    }
    void do_base58chkenc() {
        data_value();
        str = EncodeBase58Check(data);
        type = T_STRING;
    }
    void do_base58chkdec() {
        if (type != T_STRING) {
            fprintf(stderr, "cannot base58-decode non-string value\n");
            return;
        }
        if (!DecodeBase58Check(str, data)) {
            fprintf(stderr, "decode failed\n");
        }
        type = T_DATA;
    }
    void do_addr_to_spk() {
        // addresses are base58-check encoded, so we decode them first
        do_base58chkdec();
        // they are now prefixed with a 0x00; rip that out
        data.erase(data.begin());
        // wrap in appropriate script fluff
        CScript s;
        s << OP_DUP << OP_HASH160 << data << OP_EQUALVERIFY << OP_CHECKSIG;
        data.clear();
        insert(data, s);
    }
    void do_spk_to_addr() {
        // data should be OP_DUP OP_HASH160 0x14 <20 b hash> OP_EQUALVERIFY OP_CHECKSIG
        if (data.size() != 25) {
            fprintf(stderr, "wrong length (expected 25 bytes)\n");
            return;
        }
        if (data[0] != OP_DUP ||
            data[1] != OP_HASH160 ||
            data[2] != 0x14 ||
            data[23] != OP_EQUALVERIFY ||
            data[24] != OP_CHECKSIG) {
            fprintf(stderr, "unknown script (expected DUP H160 0x14 <20b> EQUALVERIFY CHECKSIG\n");
            return;
        }
        data[0] = 0x00; // prefix
        data.erase(data.begin() + 1, data.begin() + 3);
        data.resize(21);
        do_base58chkenc();
    }
    void do_bech32enc() {
        data_value();
        std::vector<unsigned char> tmp = {0};
        ConvertBits<8, 5, true>(tmp, data.begin(), data.end());
        str = bech32::Encode("bc", tmp);
        type = T_STRING;
    }
    void do_bech32dec() {
        if (type != T_STRING) {
            fprintf(stderr, "cannot bech32-decode non-string value\n");
            return;
        }
        auto bech = bech32::Decode(str);
        if (bech.first == "") {
            fprintf(stderr, "failed to bech32-decode string\n");
            return;
        }
        // Bech32 decoding
        int version = bech.second[0]; // The first 5 bit symbol is the witness version (0-16)
        // data = r.second;
        printf("(bech32 HRP = %s)\n", bech.first.c_str());
        type = T_DATA;
        data.clear();
        // The rest of the symbols are converted witness program bytes.
        if (ConvertBits<5, 8, false>(data, bech.second.begin() + 1, bech.second.end())) {
            if (version == 0) {
                {
                    if (data.size() == 20) {
                        // std::copy(data.begin(), data.end(), keyid.begin());
                        // return keyid;
                        return;
                    }
                }
                {
                    // WitnessV0ScriptHash scriptid;
                    if (data.size() == 32) {
                        // std::copy(data.begin(), data.end(), scriptid.begin());
                        // return scriptid;
                        return;
                    }
                }
                fprintf(stderr, "warning: unknown size %zu\n", data.size());
                // return CNoDestination();
                return;
            }
            if (version > 16 || data.size() < 2 || data.size() > 40) {
                return;
                // return CNoDestination();
            }
            // WitnessUnknown unk;
            // unk.version = version;
            // std::copy(data.begin(), data.end(), unk.program);
            // unk.length = data.size();
            // return unk;
            return;
        }
    }
    bool is_pubkey();
    void do_verify_sig();
    void do_combine_pubkeys();
    void do_tweak_pubkey();
    void do_add();
    void do_sub();
    void do_negate_pubkey();
    void do_not_op();
    void do_boolify();
    inline bool is_null_or_int(int64_t i) const {
        return (type == T_DATA && (data.size() == 0 || (data.size() == 1 && data[0] == (uint8_t)i)))
            || (type == T_INT && int64 == i);
    }
#ifdef ENABLE_DANGEROUS
    void do_combine_privkeys();
    void do_multiply_privkeys();
    void do_pow_privkey();
    void do_negate_privkey();
    void do_encode_wif() {
        data_value();
        data.insert(data.begin(), 0x80);    // main net
        // data.insert(data.end(),   0x01);    // compressed
        Value hashed(*this);
        hashed.do_hash256();
        data.insert(data.end(), hashed.data.begin(), hashed.data.begin() + 4);
        do_base58enc();
    }
    void do_decode_wif() {
        if (type != T_STRING) {
            fprintf(stderr, "input must be a WIF string; type = %d\n", type);
            return;
        }
        do_base58dec();
        if (data.size() < 4) {
            fprintf(stderr, "base58 decoding failed\n");
            return;
        }
        std::vector<uint8_t> chksum(data.end() - 4, data.end());
        data.resize(data.size() - 4);
        if (data[0] != 0x80) {
            fprintf(stderr, "unexpected prefix 0x%02x (expected 0x80)\n", data[0]);
        }
        // check sum validation part before removing prefixes/suffixes
        Value hashed(*this);
        hashed.do_hash256();
        hashed.data.resize(4);
        for (int i = 0; i < 4; i++) {
            if (hashed.data[i] != chksum[i]) {
                fprintf(stderr, "checksum failure for byte %d: 0x%02x != 0x%02x\n", i, chksum[i], hashed.data[i]);
                return;
            }
        }
        data = std::vector<uint8_t>(data.begin() + 1, data.end());
    }
    void do_sign();
    void do_get_pubkey();
#endif // ENABLE_DANGEROUS
    void print() const {
        switch (type) {
        case T_INT:
            printf("%" PRId64, int64);
            return;
        case T_OPCODE:
            printf("%s (%02x)", GetOpName(opcode), opcode);
        case T_DATA:
            for (auto it : data) printf("%02x", it);
            return;
        case T_STRING:
            printf("\"%s\"", str.c_str());
        }
    }
    void println() const {
        print(); fputc('\n', stdout);
    }
    std::string to_string() const {
        std::string s = "";
        switch (type) {
        case T_INT:
            return strprintf("%" PRId64, int64);
        case T_OPCODE:
            return strprintf("%s (%02x)", GetOpName(opcode), opcode);
        case T_DATA:
            for (auto it : data) s = s + strprintf("%02x", it);
            return s;
        case T_STRING:
            return strprintf("\"%s\"", str.c_str());
        }
        return "???";
    }
    static Value prepare_extraction(const Value& a, const Value& b);
    void calc_point(std::vector<uint8_t>& x, std::vector<uint8_t>& y);
private:
    bool extract_values(std::vector<std::vector<uint8_t>>& values);
};

#endif // included_value_h_
