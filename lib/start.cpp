#include <darkleaks.hpp>

#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <bitcoin/bitcoin.hpp>
#include "utility.hpp"
#include "aes256.h"

namespace darkleaks {

using namespace bc;
namespace fs = boost::filesystem;

// Encrypt for first hash.
data_chunk dl_encrypt(data_chunk buffer, hash_digest seed)
{
    aes256_context ctx; 
    BITCOIN_ASSERT(seed.size() == 32);
    aes256_init(&ctx, seed.data());
    BITCOIN_ASSERT(buffer.size() % 16 == 0);
    for (size_t i = 0; i < buffer.size(); i += 16)
        aes256_encrypt_ecb(&ctx, buffer.data() + i);
    aes256_done(&ctx);
    return buffer;
}

void test_decryption(const data_chunk& buffer,
    data_chunk cipher, const ec_point& addr_pubkey)
{
    hash_digest seed = derive_seed(addr_pubkey);
    aes256_context ctx; 
    BITCOIN_ASSERT(seed.size() == 32);
    aes256_init(&ctx, seed.data());
    BITCOIN_ASSERT(cipher.size() % 16 == 0);
    for (size_t i = 0; i < cipher.size(); i += 16)
        aes256_decrypt_ecb(&ctx, cipher.data() + i);
    aes256_done(&ctx);
    BITCOIN_ASSERT(buffer == cipher);
}

size_t start(
    const std::string filename,
    const std::string chunks_path,
    const size_t chunks)
{
    const fs::path document_full_path = filename;
    const fs::path public_chunks_path = chunks_path;
    std::ifstream infile(document_full_path.native(), std::ifstream::binary);
    infile.seekg(0, std::ifstream::end);
    size_t file_size = infile.tellg();
    infile.seekg(0, std::ifstream::beg);
    size_t chunk_size = file_size / chunks;
    // AES works on blocks of 16 bytes. Round up to nearest multiple.
    chunk_size += 16 - (chunk_size % 16);
    BITCOIN_ASSERT(chunk_size % 16 == 0);
    //std::cout << "Creating chunks of "
    //    << chunk_size << " bytes each." << std::endl;
    // Write the bidding address and chunks
    const fs::path bid_filename = public_chunks_path / "ADDRS";
    std::ofstream bidfile(bid_filename.native());
    size_t i = 0;
    while (infile)
    {
        data_chunk buffer(chunk_size);
        // Copy chunk to public chunk file.
        char* data = reinterpret_cast<char*>(buffer.data());
        infile.read(data, chunk_size);
        const std::string i_str = boost::lexical_cast<std::string>(i);
        const fs::path chunk_filename =
            public_chunks_path / (std::string("CHUNK.") + i_str);
        ++i;
        std::ofstream outfile(chunk_filename.native(), std::ifstream::binary);
        // Create a seed.
        BITCOIN_ASSERT(ec_secret_size == hash_size);
        ec_secret secret = bitcoin_hash(buffer);
        ec_point pubkey = secret_to_public_key(secret);
        // Once we spend funds, we reveal the decryption pubkey.
        payment_address bid_addr = bidding_address(pubkey);
        hash_digest seed = derive_seed(pubkey);
        // Should be encrypted!!
        // Use hash of pubkey as encryption key.
        if (buffer.size() < 16)
        {
            BITCOIN_ASSERT(infile.gcount() < 16);
            extend_data(buffer, data_chunk(0, 16 - buffer.size()));
        }
        BITCOIN_ASSERT(buffer.size() >= 16);
        data_chunk encrypted = dl_encrypt(buffer, seed);
        char* enc_data = reinterpret_cast<char*>(encrypted.data());
        outfile.write(enc_data, encrypted.size());
        test_decryption(buffer, encrypted, pubkey);
        // Write bidding address also.
        const std::string line = i_str + " " + bid_addr.encoded() + "\n";
        bidfile.write(line.c_str(), line.size());
    }
    return i;
}

} // namespace darkleaks

