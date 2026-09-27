#include <assert.h>
#include "../kernel/net_socket.h"

static void mapped(uint8_t out[16], uint32_t address) {
    for (uint32_t i=0;i<16;i++) out[i]=0;
    out[10]=out[11]=0xff;
    out[12]=(uint8_t)(address>>24); out[13]=(uint8_t)(address>>16);
    out[14]=(uint8_t)(address>>8); out[15]=(uint8_t)address;
}

int main(void) {
    struct net_udp_socket_table table;
    uint64_t handle=0, next=0, local_specific=0, handles[NET_UDP_SOCKET_CAPACITY];
    uint8_t src[16], dst[16], output[16], data[3]={'u','d','p'};
    struct net_udp_receive_info info;
    net_udp_socket_table_init(&table);

    assert(net_udp_socket_bind(&table,0,0,8080,&handle)==-1);
    assert(net_udp_socket_bind(&table,1,0,0,&handle)==-1);
    assert(net_udp_socket_bind(&table,1,0xffffffffU,8080,&handle)==-1);
    assert(net_udp_socket_bind(&table,1,0xe0000001U,8080,&handle)==-1);
    assert(net_udp_socket_bind(&table,1,0,8080,&handle)==0);
    assert(net_udp_socket_bind(&table,1,0xc0000202U,8080,&next)==-3);
    assert(net_udp_socket_bind(&table,2,0,8080,&next)==-3);
    assert(net_udp_socket_bind(&table,1,0xc0000202U,8081,&local_specific)==0);

    mapped(src,0xc0000201U); mapped(dst,0xc0000202U);
    assert(net_udp_socket_dispatch(&table,4,1,src,dst,40000,8080,data,3)==0);
    assert(table.delivered==1);
    assert(net_udp_socket_receive(&table,2,handle,output,sizeof(output),&info)==-1);
    assert(net_udp_socket_receive(&table,1,handle,output,2,&info)==-2);
    assert(net_udp_socket_receive(&table,1,handle,output,sizeof(output),&info)==0);
    assert(info.source_address==0xc0000201U && info.source_port==40000 &&
           info.length==3 && output[0]=='u' && output[2]=='p');
    assert(net_udp_socket_receive(&table,1,handle,output,sizeof(output),&info)==1);

    /* Bounded receive queue drops newest rather than blocking the RX path. */
    for (uint32_t i=0;i<NET_UDP_SOCKET_QUEUE_DEPTH+1U;i++)
        assert(net_udp_socket_dispatch(&table,4,1,src,dst,53,8080,data,3)==0);
    assert(table.delivered==1+NET_UDP_SOCKET_QUEUE_DEPTH);
    assert(table.dropped==1);
    assert(net_udp_socket_dispatch(&table,4,1,src,dst,53,9,data,3)==0);
    assert(table.unmatched==1);

    /* Wrong family, unmapped addresses, and oversized datagrams are rejected. */
    assert(net_udp_socket_dispatch(&table,6,1,src,dst,53,8080,data,3)==-1);
    src[0]=1;
    assert(net_udp_socket_dispatch(&table,4,1,src,dst,53,8080,data,3)==-1);
    mapped(src,0xc0000201U);
    assert(net_udp_socket_dispatch(&table,4,1,src,dst,53,8080,data,
                                   NET_UDP_SOCKET_PAYLOAD_MAX+1U)==-1);

    /* Closing invalidates capabilities; slot reuse receives a new generation. */
    assert(net_udp_socket_close(&table,2,handle)==-1);
    assert(net_udp_socket_close(&table,1,handle)==0);
    assert(net_udp_socket_receive(&table,1,handle,output,sizeof(output),&info)==-1);
    assert(net_udp_socket_bind(&table,3,0,8080,&next)==0);
    assert(next!=handle);
    assert(net_udp_socket_close(&table,3,next)==0);
    assert(net_udp_socket_close(&table,1,local_specific)==0);
    assert(table.invalid==6);

    for (uint32_t i=0;i<NET_UDP_SOCKET_CAPACITY;i++)
        assert(net_udp_socket_bind(&table,9,0,(uint16_t)(10000U+i),
                                   &handles[i])==0);
    assert(net_udp_socket_bind(&table,9,0,20000,&next)==-4);
    return 0;
}
