
/* Our own binary format for storing data */
struct __attribute__((__packed__)) FFormat
{
        int64_t unix_time;
        struct in_addr userip, host;
        uint32_t octetsin, octetsout;
        uint16_t srcport, dstport;
        uint8_t proto;
};
