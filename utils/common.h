#ifndef COMMON_H
#define COMMON_H
#include <endian.h>

typedef enum result{
    SUCCESS = 0,
    FAILURE = 1,
    UNRECOGNIZED_LSIS = 2,
    INVAID_INPUT = 3,
    NOT_MATCH = 4,
    CERTIFICATE_NOT_FOUND = 6,
    CERTIFICATE_REVOKED = 7,
    CERTIFICATE_NOT_TRUSTED = 8,
    CERTIFICATE_BADLY_FORMED = 9,
    TOO_MANY_ENTRIES_IN_PERMISSON_ARRAY = 10,
    UNSUPPORTED_REGION_TYPE_IN_CERTIFICATE = 11,
    TOO_MANY_ENTRIES_IN_POLYGONAL_GEOGRAPHIC_SCOPE = 12,
    IMPLICT_CERTIFICATE_NOT_ACCEPTABLE_AS_TRUST_ANCHOR = 13,
    UNRECOGNIZED_IDENTIFIER = 14,
    EXPIRED = 15,
    NOT_ISSUED_YET = 16,
    MISSING = 17,
    START_DATE_BEFORE_ISSUE_DATE = 18,
    ISSUE_DATE_NOT_BEFORE_NEXT_CRL_DATE = 19,
    INVAILD_CA_SIGNATURE_ALGORITHM = 20,
    OVERDUE_CRL = 21,
    FUTURE_CERTIFICATE_AT_GENERATION_TIME = 22,
    EXPIER_CERTIFICATE_AT_GENERATION_TIME = 23,
    INVALID_CERTIFICATE_TYPE = 24,
    CERTIFICATE_NOT_AUTHORIZED_FOR_SPECIFIED_CRL_SERIES = 25,
    CERTIFICATE_NOT_AUTHORIZED_TO_ISSUE_CRL_FOR_SPECIFIC_CA = 26,
    WRONG_CA_ID_IN_CRL = 27,

    EXPIRTY_TIME_BEFORE_GENERATION_TIME = 29,
    EXPIRY_DATE_TOO_EARLY = 30,
    EXPIRY_DATE_TOO_LATE = 31,
    SIGNATURE_GENERATED_OUTSIDE_CERTIFICATE_VALIDITY_REGION = 32,
    UNAUTHORIZED_PSID = 33,
    PSIDS_NOT_MATCH = 34,
    UNAUTHORIZED_CERTIFICATE_TYPE = 35,
    DATA_EXPIRED_BASE_ON_EXPIRY_TIME = 36,
    REPLY = 37,
    OUT_OF_RANGE = 38,
    SENDER_LOCATION_UNAVARILBEL = 39,
    INCONSISITENT_CA = 40,
    INCONSISITENT_CSR_PERMISSIONS = 41,
    INCONSISITENT_KEYS_IN_REQUEST = 42,
    UNEXPECTED_TYPE = 43
}result;

#define be_to_host16(n) be16toh(n)
#define be_to_host32(n) (__force u32)be32toh(n)//
#define be_to_host64(n) (__force u64)be64toh(n)
#define host_to_be16(n) (__force u16)htobe16(n)
#define host_to_be32(n) (__force u32)htobe32(n)
#define host_to_be64(n) (__force u64)htobe64(n)

/**这里的兼容行不好，后面改成系统的那写uint8**/
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef char s8;
typedef short s16;
typedef int s32;
typedef long long s64;

typedef u8 bool;
#define false 0
#define true 1

#ifdef __GNUC__
#define STRUCT_PACKED __attribute__ ((packed))
#else
#define STRUCT_PACKED

#endif/*GNUC*/
#endif /*COMMON_H*/
