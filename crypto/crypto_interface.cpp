/*=============================================================================
#
# Author: 杨广华 - edesale@qq.com
#
# QQ : 374970456
#
# Last modified: 2015-10-20 09:02
#
# Filename: crypto_interface.cpp
#
# Description: 
#
=============================================================================*/
#include "crypto_interface.h"
#include <assert.h>
#include <iostream>
#include <sstream>

using std::stringstream;
using std::cout;
using std::endl;
using std::cerr;
#include <string>
using std::string;

#include <cstdlib>
using std::exit;

#include "cryptopp/files.h"
using CryptoPP::FileSink;
using CryptoPP::FileSource;

#include "cryptopp/hex.h"
using CryptoPP::HexEncoder;

#include "cryptopp/pubkey.h"
using CryptoPP::PublicKey;
using CryptoPP::PrivateKey;
#include "cryptopp/osrng.h"
using CryptoPP::AutoSeededRandomPool;

#include "cryptopp/aes.h"
using CryptoPP::AES;

#include "cryptopp/integer.h"
using CryptoPP::Integer;

#include "cryptopp/sha.h"
using CryptoPP::SHA1;

#include "cryptopp/filters.h"
using CryptoPP::StringSource;
using CryptoPP::StringSink;
using CryptoPP::ArraySink;
using CryptoPP::SignerFilter;
using CryptoPP::SignatureVerificationFilter;
using CryptoPP::PK_EncryptorFilter;
using CryptoPP::PK_DecryptorFilter;
using CryptoPP::HashFilter;
using CryptoPP::HashVerificationFilter;

#include "cryptopp/eccrypto.h"
using CryptoPP::ECDSA;
using CryptoPP::ECP;
using CryptoPP::DL_GroupParameters_EC;
using CryptoPP::ECIES;
using CryptoPP::EC2N;
using CryptoPP::ECPPoint;
using CryptoPP::DL_GroupPrecomputation;
using CryptoPP::DL_FixedBasePrecomputation;
#include "cryptopp/pubkey.h"
using CryptoPP::DL_PrivateKey_EC;
using CryptoPP::DL_PublicKey_EC;


#include "cryptopp/asn.h"
#include "cryptopp/oids.h"
using CryptoPP::OID;

#include "cryptopp/cryptlib.h"
using CryptoPP::PK_Encryptor;
using CryptoPP::PK_Decryptor;
using CryptoPP::g_nullNameValuePairs;
using CryptoPP::Exception;

#include "cryptopp/hmac.h"
using CryptoPP::HMAC;

#include "cryptopp/sha.h"
using CryptoPP::SHA256;
using CryptoPP::SHA224;

#include "cryptopp/hex.h"
using CryptoPP::HexEncoder;
using CryptoPP::HexDecoder;

#include "cryptopp/secblock.h"
using CryptoPP::SecByteBlock;


void StringToChar(string s, char* buf)
{
	int len = s.length();
	int i ;

	for(i = 0; i < len; i++)
		*(buf+i) = s[i];
	*(buf+len) = '\0';
}
void CharToString(string& s, char* buf, int len)
{
		s.clear();
		for(int i = 0; i < len; i++)
		{
			s.push_back(*(buf+i));
		}
}

bool ECDSA_GeneratePrivateKey (const OID& oid, 
				ECDSA<ECP, SHA1>::PrivateKey& key)
{
	AutoSeededRandomPool prng;

	key.Initialize(prng, oid);
	assert(key.Validate(prng, 3));

	return key.Validate(prng, 3);
}

bool ECDSA_GeneratePublicKey(const ECDSA<ECP, SHA1>::PrivateKey& privateKey,
				ECDSA<ECP, SHA1>::PublicKey& publicKey)
{
	AutoSeededRandomPool prng;

	//Sanity check
	assert(privateKey.Validate(prng,3));

	privateKey.MakePublicKey(publicKey);
	assert(publicKey.Validate(prng, 3));

	return publicKey.Validate(prng, 3);
}
bool SignMessage(const ECDSA<ECP, SHA1>::PrivateKey& key, const string& message, 
			string& signature)
{
	AutoSeededRandomPool prng;

	signature.erase();

	StringSource(message, true,
		new SignerFilter(prng,
		ECDSA<ECP, SHA1>::Signer(key),
		new StringSink(signature)
		)	//SignerFilter
		);  //StringSource
	return !signature.empty();
}
bool VerifyMessage(const ECDSA<ECP, SHA1>::PublicKey& key, 
				const string& message, const string& signature)
{
	bool result = false;

	StringSource(signature + message, true,
		new SignatureVerificationFilter(
		ECDSA<ECP, SHA1>::Verifier(key),
		new ArraySink((byte*)&result, sizeof(result))
		) // SignatureVerificationFilter
		);

	return result;
}




int ECDSA256_get_privatekey(char* privatekey_buf, int len)
{
	bool result = false;
	ECDSA<ECP, SHA1>::PrivateKey privatekey;
	ECDSA<ECP, SHA1>::PublicKey publickey;

	result = ECDSA_GeneratePrivateKey(CryptoPP::ASN1::secp256r1(), privatekey);
	assert(true == result);
	if(!result)	{return 0;}	
	
	stringstream stream;
	string s_privatekey;
	stream <<std::hex<<privatekey.GetPrivateExponent();
	stream >> s_privatekey;
	if((int)s_privatekey.length() >= len)
		return -1;
	StringToChar(s_privatekey, privatekey_buf);
	return (int)s_privatekey.length();
}


int ECDSA256_get_publickey(char* public_key_x_buf, int xlen, char* public_key_y_buf,
				int ylen, char* private_key_buf, int prilen)
{
	Integer Integer_privatekey;
	string s_privatekey;
	
	//s_privatekey = private_key_buf;
	CharToString(s_privatekey, private_key_buf, prilen);
	//可能char中存在\0,所以不能直接进行赋值操作
	//CharToString(s_privatekey, private_key_buf, prlen);

	stringstream stream;
	stream << s_privatekey;
	stream >> Integer_privatekey;
	bool result = false;

	ECDSA<ECP, SHA1>::PrivateKey privateKey;
	ECDSA<ECP, SHA1>::PublicKey publicKey;

	result = ECDSA_GeneratePrivateKey(CryptoPP::ASN1::secp256r1(), privateKey);
	assert(true == result);
	if(!result) {return 0;}

	privateKey.SetPrivateExponent(Integer_privatekey);
	
	result = ECDSA_GeneratePublicKey(privateKey, publicKey);
	assert(true == result);
	if(!result)	{return 0;}
	stringstream streamx, streamy;
	string publickey_x, publickey_y;
	streamx <<std::hex <<publicKey.GetPublicElement().x;
	streamy <<std::hex << publicKey.GetPublicElement().y;

	streamx >> publickey_x;
	streamy >> publickey_y;

	//publickey_x.erase(--publickey_x.end());
	//publickey_y.erase(--publickey_y.end());

	if((int)publickey_x.length()>= xlen)
		return -1;
	StringToChar(publickey_x, public_key_x_buf);

	if((int)publickey_y.length()>=  ylen)
		return -1;
	StringToChar(publickey_y, public_key_y_buf);
	return (int)publickey_x.length();
}


int ECDSA256_sign_message(char* private_key_buf, int prilen, char* mess_buf, int mess_len,
				char* signed_mess_buf, int signed_mess_len)
{
	bool result = false;
	string message, signed_message;
	//message = mess_buf;
	CharToString(message, mess_buf, mess_len);

	string s_private_key;
	CharToString(s_private_key, private_key_buf, prilen);
	//s_private_key = private_key_buf;
	Integer integer_privatekey;
	stringstream stream;
	stream << s_private_key;
	stream >> integer_privatekey;
	
	ECDSA<ECP, SHA1>::PrivateKey privateKey;
	ECDSA<ECP, SHA1>::PublicKey publicKey;

	result = ECDSA_GeneratePrivateKey(CryptoPP::ASN1::secp256r1(),
					privateKey);
	//privateKey.Initialize(CryptoPP::ASN1::secp256k1, privatekey);
	assert(true == result);
	if(!result == result) {return 0;}
	
	privateKey.SetPrivateExponent(integer_privatekey);
	result = ECDSA_GeneratePublicKey(privateKey, publicKey);
	assert(true == result);
	if(!result ){return 0;}

	result = SignMessage(privateKey, message, signed_message);
	assert(true == result);
	if(result == false)
		return 0;
	if((int)signed_message.length() >= signed_mess_len)
		return -1;
	StringToChar(signed_message, signed_mess_buf);

		return (int)signed_message.length();

}



int ECDSA256_verify_message(char* public_key_x_buf, int xlen, char* public_key_y_buf, 
		int ylen, char* signed_mess_buf,int signed_mess_len, char* mess_buf, int mess_len)
{
	string public_key_xs, public_key_ys;
	CharToString(public_key_xs, public_key_x_buf, xlen);
	CharToString(public_key_ys, public_key_y_buf, ylen);
	//public_key_xs = public_key_x_buf;
	//public_key_ys = public_key_y_buf;
	string message, signed_message;
	CharToString(message, mess_buf, mess_len);
	CharToString(signed_message, signed_mess_buf, signed_mess_len);

	Integer  integer_public_x;
	Integer  integer_public_y;

	stringstream streamx, streamy;
	streamx << public_key_xs;
	streamx >> integer_public_x;

	streamy << public_key_ys;
	streamy >> integer_public_y;

	ECP::Point q;
	q.identity = false;
	q.x = integer_public_x;
	q.y = integer_public_y;

	ECDSA<ECP, SHA1>::PublicKey publicKey;
	publicKey.Initialize(CryptoPP::ASN1::secp256k1(), q);
	AutoSeededRandomPool prng;
	bool result = false;	
	result = publicKey.Validate(prng, 3);
	if(!result)
	{
		return 0;
	}
	
	result = VerifyMessage(publicKey, message, signed_message);
	assert(true == result);

	return 1;
}


int ECDSA224_get_privatekey(char* privatekey_buf, int len)
{
	bool result = false;
	ECDSA<ECP, SHA1>::PrivateKey privatekey;
	ECDSA<ECP, SHA1>::PublicKey publickey;

	result = ECDSA_GeneratePrivateKey(CryptoPP::ASN1::secp224r1(), privatekey);
	assert(true == result);
	if(!result)	{return 0;}
	
	stringstream stream;
	string s_privatekey;
	stream <<std::hex<<privatekey.GetPrivateExponent();
	stream >> s_privatekey;
	if((int)s_privatekey.length() >= len)
		return -1;
	StringToChar(s_privatekey, privatekey_buf);
	return (int)s_privatekey.length();
}


int ECDSA224_get_publickey(char* public_key_x_buf, int xlen, char* public_key_y_buf,
				int ylen, char* private_key_buf, int prilen)
{
	Integer Integer_privatekey;
	string s_privatekey;
	
	CharToString(s_privatekey, private_key_buf, prilen);

	stringstream stream;
	stream << s_privatekey;
	stream >> Integer_privatekey;
	bool result = false;

	ECDSA<ECP, SHA1>::PrivateKey privateKey;
	ECDSA<ECP, SHA1>::PublicKey publicKey;

	result = ECDSA_GeneratePrivateKey(CryptoPP::ASN1::secp224r1(), privateKey);
	assert(true == result);
	if(!result) {return 0;}

	privateKey.SetPrivateExponent(Integer_privatekey);
	
	result = ECDSA_GeneratePublicKey(privateKey, publicKey);
	assert(true == result);
	if(!result)	{return 0;}
	stringstream streamx, streamy;
	string publickey_x, publickey_y;
	streamx <<std::hex <<publicKey.GetPublicElement().x;
	streamy <<std::hex << publicKey.GetPublicElement().y;

	streamx >> publickey_x;
	streamy >> publickey_y;


	if((int)publickey_x.length()>= xlen)
		return -1;
	StringToChar(publickey_x, public_key_x_buf);

	if((int)publickey_y.length()>=  ylen)
		return -1;
	StringToChar(publickey_y, public_key_y_buf);
	return (int)publickey_x.length();
}


int ECDSA224_sign_message(char* private_key_buf, int prilen, char* mess_buf, int mess_len,
				char* signed_mess_buf, int signed_mess_len)
{
	bool result = false;
	string message, signed_message;
	CharToString(message, mess_buf, mess_len);

	string s_private_key;
	CharToString(s_private_key, private_key_buf, prilen);
	Integer integer_privatekey;
	stringstream stream;
	stream << s_private_key;
	stream >> integer_privatekey;
	
	ECDSA<ECP, SHA1>::PrivateKey privateKey;
	ECDSA<ECP, SHA1>::PublicKey publicKey;

	result = ECDSA_GeneratePrivateKey(CryptoPP::ASN1::secp224r1(),
					privateKey);
	assert(true == result);
	if(!result == result) {return 0;}
	
	privateKey.SetPrivateExponent(integer_privatekey);
	result = ECDSA_GeneratePublicKey(privateKey, publicKey);
	assert(true == result);
	if(!result ){return 0;}

	result = SignMessage(privateKey, message, signed_message);
	assert(true == result);
	if(result == false)
		return 0;
	if((int)signed_message.length() >= signed_mess_len)
		return -1;
	StringToChar(signed_message, signed_mess_buf);

		return (int)signed_message.length();

}



int ECDSA224_verify_message(char* public_key_x_buf, int xlen, char* public_key_y_buf, 
		int ylen, char* signed_mess_buf, int signed_mess_len ,char* mess_buf, int mess_len)
{
	string public_key_xs, public_key_ys;

	CharToString(public_key_xs, public_key_x_buf, xlen);
	CharToString(public_key_ys, public_key_y_buf, ylen);

	string message, signed_message;
	CharToString(message, mess_buf, mess_len);
	CharToString(signed_message, signed_mess_buf, signed_mess_len);

	Integer  integer_public_x;
	Integer  integer_public_y;

	stringstream streamx, streamy;
	streamx << public_key_xs;
	streamx >> integer_public_x;

	streamy << public_key_ys;
	streamy >> integer_public_y;

	ECP::Point q;
	q.identity = false;
	q.x = integer_public_x;
	q.y = integer_public_y;

	ECDSA<ECP, SHA1>::PublicKey publicKey;
	publicKey.Initialize(CryptoPP::ASN1::secp224r1(), q);
	AutoSeededRandomPool prng;
	bool result = false;	
	result = publicKey.Validate(prng, 3);
	if(!result)
	{
		return 0;
	}
	
	result = VerifyMessage(publicKey, message, signed_message);
	assert(true == result);

	return 1;
}


int ECIES256_get_private_key(char* private_key_buf, int prlen)
{
	AutoSeededRandomPool prng;

	ECIES<ECP>::Decryptor d0(prng, CryptoPP::ASN1::secp256r1());
	Integer private_key = d0.GetKey().GetPrivateExponent();
	ECIES<ECP>::Encryptor e0(d0);


	stringstream stream;
	string sprivatekey;
	stream <<std::hex<< private_key;
	stream >> sprivatekey;
	
	if(prlen <= (int)sprivatekey.length())
		return -1;
	StringToChar(sprivatekey, private_key_buf);
	return (int)sprivatekey.length();
}


int ECIES256_get_public_key(char* public_key_x_buf, int xlen, 
		char* public_key_y_buf,	int ylen, char* private_key_buf, int prilen)
{
	string sprivate;
	CharToString(sprivate, private_key_buf, prilen);
	
	//sprivate = private_key_buf;
	
	AutoSeededRandomPool prng;	


	Integer integer_private;
	stringstream stream;
	stream << sprivate;
	stream >> integer_private;
	ECIES<ECP>::Decryptor d0;
	d0.AccessKey().AccessGroupParameters().Initialize(CryptoPP::ASN1::secp256r1());
	d0.AccessKey().SetPrivateExponent(integer_private);
	ECIES<ECP>::Encryptor e0(d0);
	e0.GetPublicKey().ThrowIfInvalid(prng, 3);

	string public_x, public_y;
	stringstream streamx, streamy;

	Integer x = e0.GetKey().GetPublicElement().x;
	Integer y = e0.GetKey().GetPublicElement().y;

	streamx<< std::hex<<x;
	streamy<< std::hex<<y;
	
	
	streamx>> public_x;
	streamy>> public_y;


	if(xlen <= public_x.length())
		return -1;
	StringToChar(public_x, public_key_x_buf);
	if(ylen <= public_y.length())
		return -1;
	StringToChar(public_y, public_key_y_buf);

	
	return (int)public_y.length();
}

//公钥加密，私钥解密
int ECIES256_encrypto_message(char* mess_buf, int mess_len, 
		char* encrypto_mess_buf, int encrypto_mess_len, char* public_key_x_buf,
		int xlen, char* public_key_y_buf, int ylen)
{
	AutoSeededRandomPool prng;
	string message, encrypto_message;
	CharToString(message, mess_buf, mess_len);
	string public_key_x, public_key_y;
	CharToString(public_key_x, public_key_x_buf, xlen);
	CharToString(public_key_y, public_key_y_buf, ylen);

	stringstream streamx, streamy;
	streamx << public_key_x;
	streamy << public_key_y;
	
	Integer pubx, puby;
	streamx >>pubx;
	streamy >>puby;

	ECP::Point q;
	q.identity = false;
	q.x = pubx;
	q.y = puby;


	
	ECIES<ECP>::Encryptor e0;
	e0.AccessKey().AccessGroupParameters().Initialize(CryptoPP::ASN1::secp256r1());
	e0.AccessKey().SetPublicElement(q);


	e0.GetPublicKey().ThrowIfInvalid(prng, 3);
	string em0;
	StringSource ss1(message, true, new PK_EncryptorFilter(prng, e0, 
							new StringSink(em0)));

	if(encrypto_mess_len <= (int)em0.length())
	{
		return -1;
	}
	StringToChar(em0, encrypto_mess_buf);
	return (int)em0.length();
	
}


int ECIES256_decrypto_message(char* encrypto_mess_buf, int encrypto_mess_len, 
		char* decrypto_mess_buf, int decrypto_mess_len, 
		char* private_key_buf, int prilen)
{
	AutoSeededRandomPool prng;
	string s_private; 
	
	CharToString(s_private, private_key_buf, prilen);

	stringstream stream;
	Integer integer_private;
	stream << private_key_buf;
	stream >> integer_private;
	
	ECIES<ECP>::Decryptor d0;
	d0.AccessKey().AccessGroupParameters().Initialize(CryptoPP::ASN1::secp256r1());
	d0.AccessKey().SetPrivateExponent(integer_private);

	string encrypto_mess;
	CharToString(encrypto_mess, encrypto_mess_buf, encrypto_mess_len);

	string dm;


	d0.GetPrivateKey().ThrowIfInvalid(prng, 3);	

	StringSource ss2(encrypto_mess, true, new PK_DecryptorFilter(prng,
							d0, new StringSink(dm)));


	if((int)dm.length()>= decrypto_mess_len)
		return -1;

	StringToChar(dm, decrypto_mess_buf);

	return (int)dm.length();
}



int ECIES224_get_private_key(char* private_key_buf, int prlen)
{
	AutoSeededRandomPool prng;

	ECIES<ECP>::Decryptor d0(prng, CryptoPP::ASN1::secp224r1());
	Integer private_key = d0.GetKey().GetPrivateExponent();
	ECIES<ECP>::Encryptor e0(d0);


	stringstream stream;
	string sprivatekey;
	stream <<std::hex<< private_key;
	stream >> sprivatekey;
	
	if(prlen <= (int)sprivatekey.length())
		return -1;
	StringToChar(sprivatekey, private_key_buf);
	return (int)sprivatekey.length();
}


int ECIES224_get_public_key(char* public_key_x_buf, int xlen, 
		char* public_key_y_buf,	int ylen, char* private_key_buf, int prilen)
{
	string sprivate;
	CharToString(sprivate, private_key_buf, prilen);
	
	//sprivate = private_key_buf;
	
	AutoSeededRandomPool prng;	


	Integer integer_private;
	stringstream stream;
	stream << sprivate;
	stream >> integer_private;
	ECIES<ECP>::Decryptor d0;
	d0.AccessKey().AccessGroupParameters().Initialize(CryptoPP::ASN1::secp224r1());
	d0.AccessKey().SetPrivateExponent(integer_private);
	ECIES<ECP>::Encryptor e0(d0);
	e0.GetPublicKey().ThrowIfInvalid(prng, 3);

	string public_x, public_y;
	stringstream streamx, streamy;

	Integer x = e0.GetKey().GetPublicElement().x;
	Integer y = e0.GetKey().GetPublicElement().y;

	streamx<< std::hex<<x;
	streamy<< std::hex<<y;
	
	
	streamx>> public_x;
	streamy>> public_y;


	if(xlen <= public_x.length())
		return -1;
	StringToChar(public_x, public_key_x_buf);
	if(ylen <= public_y.length())
		return -1;
	StringToChar(public_y, public_key_y_buf);

	
	return (int)public_y.length();
}

//公钥加密，私钥解密
int ECIES224_encrypto_message(char* mess_buf, int mess_len, 
		char* encrypto_mess_buf, int encrypto_mess_len, char* public_key_x_buf,
		int xlen, char* public_key_y_buf, int ylen)
{
	AutoSeededRandomPool prng;
	string message, encrypto_message;
	CharToString(message, mess_buf, mess_len);
	string public_key_x, public_key_y;
	CharToString(public_key_x, public_key_x_buf, xlen);
	CharToString(public_key_y, public_key_y_buf, ylen);

	stringstream streamx, streamy;
	streamx << public_key_x;
	streamy << public_key_y;
	
	Integer pubx, puby;
	streamx >>pubx;
	streamy >>puby;

	ECP::Point q;
	q.identity = false;
	q.x = pubx;
	q.y = puby;


	
	ECIES<ECP>::Encryptor e0;
	e0.AccessKey().AccessGroupParameters().Initialize(CryptoPP::ASN1::secp224r1());
	e0.AccessKey().SetPublicElement(q);


	e0.GetPublicKey().ThrowIfInvalid(prng, 3);
	string em0;
	StringSource ss1(message, true, new PK_EncryptorFilter(prng, e0, 
							new StringSink(em0)));

	if(encrypto_mess_len <= (int)em0.length())
	{
		return -1;
	}
	StringToChar(em0, encrypto_mess_buf);
	return (int)em0.length();
	
}


int ECIES224_decrypto_message(char* encrypto_mess_buf, int encrypto_mess_len, 
		char* decrypto_mess_buf, int decrypto_mess_len, 
		char* private_key_buf, int prilen)
{
	AutoSeededRandomPool prng;
	string s_private;
	
	CharToString(s_private, private_key_buf, prilen);

	stringstream stream;
	Integer integer_private;
	stream << private_key_buf;
	stream >> integer_private;
	
	ECIES<ECP>::Decryptor d0;
	d0.AccessKey().AccessGroupParameters().Initialize(CryptoPP::ASN1::secp224r1());
	d0.AccessKey().SetPrivateExponent(integer_private);

	string encrypto_mess;
	CharToString(encrypto_mess, encrypto_mess_buf, encrypto_mess_len);

	string dm;


	d0.GetPrivateKey().ThrowIfInvalid(prng, 3);	

	StringSource ss2(encrypto_mess, true, new PK_DecryptorFilter(prng,
							d0, new StringSink(dm)));


	if((int)dm.length()>= decrypto_mess_len)
		return -1;

	StringToChar(dm, decrypto_mess_buf);

	return (int)dm.length();
}






int HASH256_message(char* message, int len, 
				char* hash_message, int hash_len)
{
	AutoSeededRandomPool prng;
	
	SecByteBlock key(16);
	prng.GenerateBlock(key, key.size());

	string plain;
	CharToString(plain, message, len);
	string mac, encoded;

	encoded.clear();
	StringSource(key, key.size(), true,
					new HexEncoder(
					new StringSink(encoded)
					)
				); //StringSource

	try
	{
		HMAC<SHA256> hmac(key, key.size());

		StringSource(plain, true, 
			new HashFilter(hmac, 
			new StringSink(mac)
			)
			);
	}
	catch(const CryptoPP::Exception& e)
	{
		cerr<<e.what()<<endl;
		return 0;
		exit(1);
	}

	encoded.clear();
	StringSource(mac, true, 
			new HexEncoder(
			new StringSink(encoded)
			)
			);

	if(hash_len <= encoded.length())
		return -1;
	StringToChar(encoded, hash_message);
	return (int)encoded.length();
}



int HASH224_message(char* message, int len, 
				char* hash_message, int hash_len)
{
	AutoSeededRandomPool prng;
	
	SecByteBlock key(16);
	prng.GenerateBlock(key, key.size());

	string plain;
	CharToString(plain, message, len);
	string mac, encoded;

	encoded.clear();
	StringSource(key, key.size(), true,
					new HexEncoder(
					new StringSink(encoded)
					)
				); //StringSource

	try
	{
		HMAC<SHA224> hmac(key, key.size());

		StringSource(plain, true, 
			new HashFilter(hmac, 
			new StringSink(mac)
			)
			);
	}
	catch(const CryptoPP::Exception& e)
	{
		cerr<<e.what()<<endl;
		return 0;
		exit(1);
	}

	encoded.clear();
	StringSource(mac, true, 
			new HexEncoder(
			new StringSink(encoded)
			)
			);

	if(hash_len <= encoded.length())
		return -1;
	StringToChar(encoded, hash_message);
	return (int)encoded.length();
}


