/*************
 *作者：大力
 */
#include"cmp.h"
#include<pthread.h>
#include<stddef.h>
#include "../utils/common.h"
#include "../cme/cme.h"
#include "../pssme/pssme.h"
#include "../sec/sec.h"
#include "../utils/debug.h"
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "../data/data_handle.h"
#define FORWARD 60  //单位是s
#define OVERDUE 60*10 // 单位是s
#define NETWORK_DELAY 60*2 // 单位是s
#define CRL_REQUSET_LEN 100 //单位字节
#define RECIEVE_DATA_MAXLEN 1000
#define TO_FILE_LEN 100
#define INIT(m) memset(&m,0,sizeof(m))
struct crl_req_time{
    struct list_head list;
    time32 time;//单位s
    hashedid8 issuer;
    crl_series crl_series;
    time32 issue_date;
};

struct cmp_db{
    struct crl_req_time crl_time;
    time32 crl_request_issue_date;
    hashedid8 crl_request_issuer;
    crl_series crl_request_crl_series;
    
    cmh req_cert_cmh;
    cmh req_cert_enc_cmh;
    struct pssme_lsis_array req_cert_lsises;

    string identifier;
    geographic_region region;
    u32 life_time;//单位day
    
    cmh ca_cmh;
    certificate ca_cert;

    u32 pending;
    pthread_mutex_t lock;
};

enum pending_flags{
    CRL_REQUEST = 1<<0,
    CERTFICATE_REQUEST = 1<<1,
    RECIEVE_DATA = 1<<2,
};

pthread_cond_t pending_cond = PTHREAD_COND_INITIALIZER;
struct cmp_db* cmdb = NULL;

    //*((typeof(cmdb->ca_cmh)*)mbuf) = cmdb->ca_cmh;
#define ptr_host_to_be(ptr,n) do{\
    switch(sizeof(n)){\
        case 1:\
            *((  typeof(n)*)ptr) = n;\
            ptr = (u8*)ptr + 1;\
            break;\
        case 2:\
            *((  typeof(n)*)ptr) = host_to_be16(n);\
            ptr = (u8*)ptr + 2;\
            break;\
        case 4:\
            *((  typeof(n)*)ptr) = host_to_be32(n);\
            ptr = (u8*)ptr + 4;\
            break;\
        case 8:\
            *(( typeof(n)*)ptr) = host_to_be64(n);\
            ptr = (u8*)ptr + 8;\
            break;\
    }\
}while(0)
#define ptr_be_to_host(ptr,n) {\
     switch(sizeof(n)){\
        case 1:\
            *(u8*)ptr = n;\
            break;\
        case 2:\
            *(u16*)ptr = be_to_host16(n);\
            break;\
        case 4:\
            *(u32*)ptr = be_to_host32(n);\
            break;\
        case 8:\
            *(u64*)ptr = be_to_host64(n);\
            break;\
    }\
}
/*************
 *这里的所有编码原则，都是固定长度，我们固定编码。
 *其他数组或者链表之类的我们就用4字节表示后面有多少字节。
 ************/
static int cert_2_file(struct cmp_db* cmdb,int fd){
    u8 *mbuf,*buf;
    int needlen,size = 0;
    buf = NULL;
    needlen = 500;//预估一下有多长.
    buf =(u8*)malloc(needlen);
    if(buf == NULL)
        goto fail;
    mbuf = buf;
    pthread_mutex_lock(&cmdb->lock);
    ptr_host_to_be(mbuf,cmdb->ca_cmh);
    size = certificate_2_buf(&cmdb->ca_cert,mbuf,needlen-size);
    if(size < 0){
        pthread_mutex_unlock(&cmdb->lock);
        wave_error_printf("证书编码的时候错误了");
        goto fail;
    }
    size += sizeof(cmdb->ca_cmh);
    pthread_mutex_unlock(&cmdb->lock);
    if(write(fd,buf,size) != size){
        wave_error_printf("写入文件发生错误");
        goto fail;
    }
    free(buf);
    return 0;
fail:
    if(buf != NULL)
        free(buf);
    return -1;
}
static int others_2_file(struct cmp_db* cmdb,int fd){
    u8  *mbuf,*buf=NULL;
    int needlen,i;
    pthread_mutex_lock(&cmdb->lock);
    needlen =   cmdb->identifier.len + 4+
                sizeof(cmdb->region.region_type)+sizeof(u32);
    switch(cmdb->region.region_type){
        case FROM_ISSUER:
        case CIRCLE:
            needlen += sizeof(s32)*2 + sizeof(u16);
            break;
        case RECTANGLE:
            needlen = needlen + cmdb->region.u.rectangular_region.len * (sizeof(s32)*4) +  4;
            break;
        case POLYGON:
            needlen = needlen + cmdb->region.u.polygonal_region.len * (sizeof(s32) *2) + 4;
            break;
        case NONE:
            needlen = needlen + 4 + cmdb->region.u.other_region.len;
            break;
        default:
            pthread_mutex_unlock(&cmdb->lock);
            goto fail;
    }
    buf = (u8*)malloc(needlen);
    if(buf == NULL){
        pthread_mutex_unlcok(&cmdb->lock);
        goto fail;
    }
    mbuf = buf;
    *(u32*)mbuf = host_to_be32(cmdb->identifier.len);
    mbuf += 4;
    memcpy(mbuf,cmdb->identifier.buf,cmdb->identifier.len);
    mbuf += cmdb->identifier.len;

    ptr_host_to_be(mbuf,cmdb->region.region_type);
    switch(cmdb->region.region_type){
        case FROM_ISSUER:
        case CIRCLE:
            ptr_host_to_be(mbuf,cmdb->region.u.circular_region.center.latitude);
            ptr_host_to_be(mbuf,cmdb->region.u.circular_region.center.longitude);
            ptr_host_to_be(mbuf,cmdb->region.u.circular_region.radius);
            break;
        case RECTANGLE:
            ptr_host_to_be(mbuf,(u32)(cmdb->region.u.rectangular_region.len));
            for(i=0;i<cmdb->region.u.rectangular_region.len;i++){
                ptr_host_to_be(mbuf,(cmdb->region.u.rectangular_region.buf+i)->north_west.latitude);
                ptr_host_to_be(mbuf,(cmdb->region.u.rectangular_region.buf+i)->north_west.longitude);
                ptr_host_to_be(mbuf,(cmdb->region.u.rectangular_region.buf+i)->south_east.latitude);
                ptr_host_to_be(mbuf,(cmdb->region.u.rectangular_region.buf+i)->south_east.longitude);
            }
            break;
        case POLYGON:
            ptr_host_to_be(mbuf,(u32)(cmdb->region.u.polygonal_region.len));
            for(i=0;i<cmdb->region.u.polygonal_region.len;i++){
                ptr_host_to_be(mbuf,(cmdb->region.u.polygonal_region.buf+i)->latitude);
                ptr_host_to_be(mbuf,(cmdb->region.u.polygonal_region.buf+i)->longitude);
            }
            break;
        case NONE:
            *(u32*)mbuf = host_to_be32(cmdb->region.u.other_region.len);
            mbuf += 4;
            memcpy(mbuf,cmdb->region.u.other_region.buf,cmdb->region.u.other_region.len);
            break;
        default:
            pthread_mutex_unlock(&cmdb->lock);
            goto fail;
    }
    ptr_host_to_be(mbuf,cmdb->life_time);
    pthread_mutex_unlock(&cmdb->lock);
    if ( write(fd,buf,needlen) != needlen){
        wave_error_printf("文件写入失败");
        goto fail;
    }
    free(buf);
    return 0;
fail:
    if(buf != NULL)
        free(buf);
    return -1;
}

static int lsis_array_2_file(struct cmp_db* cmdb,int fd){
    u8  *mbuf,*buf = NULL;
    int needlen,i;

    pthread_mutex_lock(&cmdb->lock);
    needlen = cmdb->req_cert_lsises.len;
    needlen = sizeof(pssme_lsis) * needlen;
    buf = (u8*)malloc(needlen+4);
    if(buf == NULL){
        pthread_mutex_unlcok(&cmdb->lock);
        goto fail;
    }
    mbuf = buf+4;
    for(i=0;i<cmdb->req_cert_lsises.len;i++)
        ptr_host_to_be(mbuf,*(cmdb->req_cert_lsises.lsis+i));
    *(u32*)buf = host_to_be32(cmdb->req_cert_lsises.len);
    pthread_mutex_unlock(&cmdb->lock);
    if( write(fd,buf,needlen+4) != needlen+4){
        goto fail;
    }
    free(buf);
    return needlen + 4;
fail:
    if(buf != NULL)
        free(buf);
    return -1;
}
static int crl_cert_request_2_file(struct cmp_db* cmdb,int fd){
    u8 *mbuf,*buf;
    int needlen = sizeof(time32) + 8 + sizeof(crl_series) +
        2*sizeof(cmh);
    if( (buf = (u8*)malloc(needlen)) == NULL){
        wave_error_printf("分配失败");
        return -1;
    }
    mbuf = buf;
    pthread_mutex_lock(&cmdb->lock);
    ptr_host_to_be(mbuf,cmdb->crl_request_issue_date);
    memcpy(mbuf,cmdb->crl_request_issuer.hashedid8,8);
    mbuf += 8;
    ptr_host_to_be(mbuf,cmdb->crl_request_crl_series);
    ptr_host_to_be(mbuf,cmdb->req_cert_cmh);
    ptr_host_to_be(mbuf,cmdb->req_cert_enc_cmh);
    pthread_mutex_unlock(&cmdb->lock);
    if( write(fd,buf,needlen) != needlen){
        wave_error_printf("写入失败");
        goto fail;
    }
    free(buf);
    return needlen;
fail:
    free(buf);
    return -1;
}
/**
 * 这个是固定长度，我们编码成固定长度
 */
static int crl_req_time_2_file(struct crl_req_time* ptr,int fd){
    int needlen ;
    u8 *buf,*obuf;
    needlen = sizeof(time32) + 8 +sizeof(crl_series) + sizeof(time32);
    if( (buf = (u8*)malloc(needlen)) == NULL) {
        wave_error_printf("空间分配失败");
        return -1;
    }
    obuf = buf;
    ptr_host_to_be(buf,ptr->time);
    memcpy(buf,&ptr->issuer.hashedid8,8);
    buf +=8;
    ptr_host_to_be(buf,ptr->crl_series);
    ptr_host_to_be(buf,ptr->issue_date);
    if( write(fd,obuf,needlen) != needlen){
        free(obuf);
        wave_error_printf("写入失败");
        return -1;
    }
    free(obuf);
    return 1;
}
/**
 * 所有的长度都用四字节编码
 */
static int crl_req_time_list_2_file(struct cmp_db* cmdb,int fd){
    struct list_head *head;
    struct crl_req_time* ptr;
    u32 size,temp;
    
    size = 0;
    pthread_mutex_lock(&cmdb->lock);
    head = &cmdb->crl_time.list;
    list_for_each_entry(ptr,head,list){
        if( (temp = crl_req_time_2_file(ptr,fd)) < 0){
            wave_printf_fl(MSG_ERROR,"crl_req_time_2_file 失败");
            pthread_mutex_unlock(&cmdb->lock);
            return -1;
        }
        size +=1;
    }
    pthread_mutex_unlock(&cmdb->lock);
    size = host_to_be32(size);
    if( file_insert(fd,0,&size,4) ){
        wave_error_printf("文件插入出错");
        return -1;
    }
    if ( lseek(fd,4,SEEK_CUR) == -1){
        wave_error_printf("文件移动偏移量出问题");
        return -1;
    }
    return 1;
}
static void cmp_db_2_file(struct cmp_db* cmdb,const char* name){
    int fd;
    if (( fd = open(name,O_WRONLY|O_CREAT|O_TRUNC) ) <0 ){
        wave_printf(MSG_ERROR,"打开文件 %s 失败",name);
        return ;
    } 
    if ( crl_req_time_list_2_file(cmdb,fd) <0)
        goto fail;
    if ( crl_cert_request_2_file(cmdb,fd) < 0)
        goto fail;
    if( lsis_array_2_file(cmdb,fd) < 0)
        goto fail;
    if( others_2_file(cmdb,fd) < 0)
        goto fail;
    if( cert_2_file(cmdb,fd) < 0)
        goto fail;
    close(fd);
fail:
    close(fd);
}
static int inline file_2_len(u32* len,int fd){
    if(read(fd,len,4) != 4){
        wave_error_printf("读取长度失败");
        return -1;
    }
    *len = be_to_host32(*len);
    return 0;
}
static int inline file_2_element(int fd,void* ele,int size){
    if(read(fd,ele,size) != size){
        wave_error_printf("读取长度失败");
        return -1;
    }
     switch(size){
        case 1:
            *(u8*)ele = *(u8*)ele;
            break;
        case 2:
            *(u16*)ele = be_to_host16( *(u16*)ele );
            break;
        case 4:
            *(u32*)ele = be_to_host32( *(u32*)ele );
            break;
        case 8:
            *(u64*)ele = be_to_host64( *(u64*)ele );
            break;
    }
    return 0;
}
static int file_2_ca_cert(struct cmp_db* cmdb,int fd){
    u8* buf = NULL;
    u32 len = 400;//预估cerficate有多大。
    u32 size;
    buf = (u8*)malloc(len);
    if(buf == NULL){
        wave_error_printf("内存分配失败");
        return -1;
    }
    pthread_mutex_lock(&cmdb->lock);
    if( file_2_element(fd,&cmdb->ca_cmh,sizeof(cmdb->ca_cmh))){
        pthread_mutex_unlock(&cmdb->lock);
        return -1;
    }

    if( (size = read(fd,buf,len)) < 0 ){
        wave_error_printf("读取失败");
        pthread_mutex_unlock(&cmdb->lock);
        return -1;
    }
    if(buf_2_certificate(&cmdb->ca_cert,buf,size)){
        certificate_free(&cmdb->ca_cert);
        wave_error_printf("cmpd证书读取失败");
        pthread_mutex_unlock(&cmdb->lock);
        return -1;
    }
    pthread_mutex_unlock(&cmdb->lock);
    return 0;
}
static int file_2_others(struct cmp_db* cmdb,int fd){
    u32 len,i;
    if( file_2_len(&len,fd)){
        return -1;
    }
    pthread_mutex_lock(&cmdb->lock);
    cmdb->identifier.len = len;
    cmdb->identifier.buf = (u8*)malloc(len);
    if(cmdb->identifier.buf == NULL){
        wave_error_printf("分配失败");
        pthread_mutex_unlock(&cmdb->lock);
        return -1;
    }
    if( read(fd,cmdb->identifier.buf,len) != len){
        wave_error_printf("读取文件失败");
        pthread_mutex_unlock(&cmdb->lock);
        return -1;
    }
    if(file_2_element(fd,&cmdb->region.region_type,sizeof(cmdb->region.region_type))){
        pthread_mutex_unlock(&cmdb->lock);
        return -1;
    }
    switch(cmdb->region.region_type){
        case FROM_ISSUER:
        case CIRCLE:
            if( file_2_element(fd,&cmdb->region.u.circular_region.center.latitude,
                        sizeof(cmdb->region.u.circular_region.center.latitude)) ||
                file_2_element(fd,&cmdb->region.u.circular_region.center.longitude,
                    sizeof(cmdb->region.u.circular_region.center.longitude)) ||
                file_2_element(fd,&cmdb->region.u.circular_region.radius,
                    sizeof(cmdb->region.u.circular_region.radius))){
                wave_error_printf("读取文件失败");
                pthread_mutex_unlock(&cmdb->lock); 
                return -1;      
            }
            break;
        case RECTANGLE:
            if ( file_2_len(&len,fd) ){
                pthread_mutex_unlock(&cmdb->lock);
                return -1;
            }
            cmdb->region.u.rectangular_region.len = len;
            cmdb->region.u.rectangular_region.buf = (struct rectangular_region*)malloc(
                   sizeof(struct rectangular_region) * len);
            if(cmdb->region.u.rectangular_region.buf == NULL){
                wave_error_printf("内存空间分配失败");
                pthread_mutex_unlock(&cmdb->lock);
                return -1;
             }
             for(i=0;i<len;i++){
                if( file_2_element(fd,&((cmdb->region.u.rectangular_region.buf+i)->north_west.latitude),sizeof(s32)) ||
                        file_2_element(fd,&((cmdb->region.u.rectangular_region.buf+i)->north_west.longitude),sizeof(s32)) ||
                        file_2_element(fd,&((cmdb->region.u.rectangular_region.buf+i)->south_east.latitude),sizeof(s32)) ||
                        file_2_element(fd,&((cmdb->region.u.rectangular_region.buf+i)->south_east.longitude),sizeof(s32))){ 
                    free(cmdb->region.u.rectangular_region.buf);
                    wave_error_printf("读取文件失败");
                    pthread_mutex_unlock(&cmdb->lock);
                    return -1;
                }
            }
            break;
        case POLYGON:
            if( file_2_len(&len,fd) ){
                pthread_mutex_unlock(&cmdb->lock);
                return -1;
            }
            cmdb->region.u.polygonal_region.len = len;
            cmdb->region.u.polygonal_region.buf = (two_d_location*)malloc(sizeof(two_d_location) * len);
            if(cmdb->region.u.polygonal_region.buf == NULL){
                wave_error_printf("内存分配失败");
                pthread_mutex_unlock(&cmdb->lock);
                return -1;
            }
            if( file_2_element(fd,&(cmdb->region.u.polygonal_region.buf + i)->latitude,sizeof(s32)) ||
                        file_2_element(fd,&(cmdb->region.u.polygonal_region.buf+i)->longitude,sizeof(s32) )){
                free(cmdb->region.u.polygonal_region.buf);
                wave_error_printf("读取文件失败");
                pthread_mutex_unlock(&cmdb->lock); 
                return -1;      
            }
            break;
        case NONE:
            if( file_2_len(&len,fd) ){
                pthread_mutex_unlock(&cmdb->lock);
                return -1;
            }
            cmdb->region.u.other_region.len = len;
            cmdb->region.u.other_region.buf = (u8*)malloc(len);
            if(cmdb->region.u.other_region.buf == NULL){
                wave_error_printf("内存分配失败");
                pthread_mutex_unlock(&cmdb->lock);
                return -1;
            }
            if( read(fd,cmdb->region.u.other_region.buf,len) != len){
                free(cmdb->region.u.other_region.buf);
                wave_error_printf("读取文件失败");
                pthread_mutex_unlock(&cmdb->lock);
                return -1;
            }
            break;
        default:
            pthread_mutex_unlock(&cmdb->lock);
            return -1;
    }

    if(file_2_element(fd,&cmdb->life_time,sizeof(cmdb->life_time))){
        wave_error_printf("读取文件失败");
        pthread_mutex_unlock(&cmdb->lock);
        return -1;
    } 
    pthread_mutex_unlock(&cmdb->lock);
    return 0;
}
static int file_2_lsis_array(struct cmp_db* cmdb,int fd){
    u32 len;
    int i;
    if(file_2_len(&len,fd) ){
        wave_error_printf("读取失败");
        return -1;
    }
    pthread_mutex_lock(&cmdb->lock);
    cmdb->req_cert_lsises.len = len;
    cmdb->req_cert_lsises.lsis = (pssme_lsis*)malloc(sizeof(pssme_lsis) * len);
    if(cmdb->req_cert_lsises.lsis == NULL){
        pthread_mutex_unlock(&cmdb->lock);
        wave_error_printf("分配空间失败");
        return -1;
    }
    for(i=0;i<len;i++){
        if(file_2_element(fd,cmdb->req_cert_lsises.lsis+i,sizeof( pssme_lsis))  ){
            wave_error_printf("读取文件失败");
            free(cmdb->req_cert_lsises.lsis);
            pthread_mutex_unlock(&cmdb->lock);
            return -1;
        }
    }
    pthread_mutex_unlock(&cmdb->lock);
    return 0;
}
static int file_2_crl_cert(struct cmp_db* cmdb,int fd){
    pthread_mutex_lock(&cmdb->lock);
    if( file_2_element(fd,&cmdb->crl_request_issue_date,sizeof(time32)) ){
        wave_error_printf("读取数据错误");
        goto fail;
    }
    if( read(fd,&cmdb->crl_request_issuer.hashedid8,8) != 8){
        wave_error_printf("读取数据错误");
        goto fail;
    }
    if( file_2_element(fd,&cmdb->crl_request_crl_series,sizeof(crl_series)) ||
            file_2_element(fd,&cmdb->req_cert_cmh,sizeof(cmh)) ||
            file_2_element(fd,&cmdb->req_cert_enc_cmh ,sizeof(cmh)) ){
        wave_error_printf("读取数据失败");
        goto fail;
    }
    pthread_mutex_unlock(&cmdb->lock);
    return 0;
fail:
    pthread_mutex_unlock(&cmdb->lock);
    return -1;
}
static struct crl_req_time* file_2_crl_req_time(int fd){
    struct crl_req_time* crt =NULL;
    time32 time;
    u8 issuer[8];
    crl_series crl_series;
    time32 issue_date;
    crt = (struct crl_req_time*)malloc(sizeof(struct crl_req_time));
    if(crt == NULL){
        wave_error_printf("内存分配失败");
        return NULL;
    }
    if(file_2_element(fd,&crt->time,sizeof(time32)) ||
            read(fd,crt->issuer.hashedid8,8) != 8||
            file_2_element(fd,&crt->crl_series,sizeof(crl_series))  ||
            file_2_element(fd,&crt->issue_date,sizeof(issue_date)) ){
        wave_error_printf("文件读取失败");
        goto fail;
    }
    return crt;
fail:
    if(crt != NULL)
        free(crt);
    return NULL;
}

static int file_2_crl_req_time_list(struct cmp_db* cmdb,int fd){
    u32 len;
    struct crl_req_time *node;
    struct list_head* head = NULL;
    int i;
    if ( file_2_len(&len,fd)){
        wave_error_printf("长度读取失败");
        return -1;
    }
    pthread_mutex_lock(&cmdb->lock);
    head  = &cmdb->crl_time.list;
    for(i=0;i<len;i++){
        node = file_2_crl_req_time(fd);
        if(node == NULL){
            pthread_mutex_unlock(&cmdb->lock);
            goto fail;
        }
        list_add_tail(&node->list,head);
    }
    pthread_mutex_unlock(&cmdb->lock);
    return 0;
fail:
   if(head != NULL){
        while( !list_empty(head)){
            node = list_entry(head->next,struct crl_req_time,list);
            list_del(&node->list);
            free(node);
            node = NULL;
        }
   } 
   return -1;
}
static int file_2_cmp_db(struct cmp_db* cmdb,const char* name){
    int fd;
    if( (fd = open(name,O_RDONLY)) <0){
        wave_error_printf("文件 %s 打开失败\n",name);
        close(fd);
        return -2;
    }
    if( file_2_crl_req_time_list(cmdb,fd) ||
            file_2_crl_cert(cmdb,fd) ||
            file_2_lsis_array(cmdb,fd) ||
            file_2_others(cmdb,fd) ||
            file_2_ca_cert(cmdb,fd)){
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}
static void crl_req_time_insert(struct cmp_db* cmdb,struct crl_req_time* new){
    struct crl_req_time *head,*ptr;
    pthread_mutex_lock(&cmdb->lock);
    head = &cmdb->crl_time;    
    //插入,按照时间排序
    list_for_each_entry(ptr,&head->list,list){
        if(ptr->time > new->time){
            break;
        }
    }
    list_add_tail(&new->list,&ptr->list);
    pthread_mutex_unlock(&cmdb->lock);
}

static void pending_crl_request(struct cmp_db* cmdb){
    pthread_mutex_lock(&cmdb->lock);
    cmdb->pending |= CRL_REQUEST;
    pthread_mutex_unlcok(&cmdb->lock);

    pthread_cond_signal(&pending_cond);
}
static void crl_alarm_handle(int signo);
static void set_crl_request_alarm(struct cmp_db* cmdb){
    time_t now,diff;
    time32 next_time;
    struct crl_req_time *head,*first;
    time(&now);
    pthread_mutex_lock(&cmdb->lock);
    head = &cmdb->crl_time;
    do{
        if(list_empty(&head->list)){
            wave_printf(MSG_WARNING,"cmp的crl_request链表为空");
            pthread_mutex_unlock(&cmdb->lock);
            return;
        }
        first = list_entry(head->list.next,struct crl_req_time,list);
        next_time = first->time;
        list_del(&first->list);
        if(next_time - FORWARD < now){
            wave_printf(MSG_WARNING,"cmp的crl_request链表第一个请求时间小于现在时间 请求时间为:%d "
                    "下次请求时间：%d",first->time,first->time + first->crl_series);
            first->time += first->crl_series;
            crl_req_time_insert(cmdb,first);
        }
        else{
            cmdb->crl_request_crl_series = first->crl_series;
            hashedid8_cpy(&cmdb->crl_request_issuer,&first->issuer);
            cmdb->crl_request_issue_date = first->issue_date;
            wave_printf(MSG_INFO,"插入一个crl_request  crl_series:%d issuer:HASHEDID8_FORMAT "
                    "issue data:%d",first->crl_series,HASHEDID8_VALUE(first->issuer),first->issue_date);
            free(first);
        }
    }while(next_time -FORWARD < now );
    pthread_mutex_unlock(&cmdb->lock);

    signal(SIGALRM,crl_alarm_handle);
    alarm(now - next_time + FORWARD);
}
static void crl_alarm_handle(int signo){
    if(signo == SIGALRM){
        pending_crl_request(cmdb);
    }
}
 
static void pending_certificate_request(struct cmp_db* cmdb){
    pthread_mutex_lock(&cmdb->lock);
    cmdb->pending |= CERTFICATE_REQUEST;
    pthread_mutex_unlock(&cmdb->lock);

    pthread_cond_signal(&pending_cond);
}
static void pending_recieve_data(struct cmp_db* cmdb){
    pthread_mutex_lock(&cmdb->lock);
    cmdb->pending |= RECIEVE_DATA;
    pthread_mutex_unlcok(&cmdb->lock);

    pthread_cond_signal(&pending_cond);
}
void cmp_do_certificate_applycation(){
    pending_certificate_request(cmdb);
}
void cmp_do_recieve_data(){
    pending_recieve_data(cmdb);
}

u32 cmp_init(){
    int res;
    cmdb = (struct cmp_db*)malloc(sizeof(struct cmp_db));
    if(cmdb == NULL)
        return -1;
    INIT(cmdb);
    cmdb->pending = 0;
    pthread_mutex_init(&cmdb->lock,NULL);
    INIT_LIST_HEAD(&cmdb->crl_time.list);
    //cmdb->indentifier == ??????车牌好，这个就是个名字还是同意发送
    //geographic_region region 这个获得怎么获得？？？
    cmdb->life_time = 10;
    cmdb->ca_cmh = 1;
    //ca_cert证书来字外部，，

    res = file_2_cmp_db(cmdb,"./cmp_db.txt");
    if( res == -2){
        return file_2_cmp_db(cmdb,"./cmp_db.init");
    }
    return res;
}
void cmp_end(){
    cmp_db_2_file(cmdb,"./cmp_db.txt");
}
static int generate_cert_request(struct sec_db* sdb,struct cmp_db* cmdb,cme_lsis lsis,
                            string* veri_pk,string* enc_pk,string* res_pk,
                            string* data,certid10* request_hash){
    serviceinfo_array serviceinfos;
    struct cme_permissions permissions;
    psid_priority_ssp* pps;
    int i;
    time32 expire;
    time_t now;

    INIT(serviceinfos);
    INIT(permissions);

    if(pssme_get_serviceinfo(sdb,lsis,&serviceinfos)){
        wave_printf(MSG_ERROR,"pssme get serviceinfo 失败");
        goto fail;
    }

    permissions.type = PSID_PRIORITY_SSP;
    permissions.u.psid_priority_ssp_array.buf =
        (struct psid_priority_ssp*)malloc(sizeof(struct psid_priority_ssp) * serviceinfos.len);
    permissions.u.psid_priority_ssp_array.len = serviceinfos.len;
    pps = permissions.u.psid_priority_ssp_array.buf;

    if(pps == NULL)
        goto fail;
    for(i=0;i<serviceinfos.len;i++){
        (pps +i)->psid =
            (serviceinfos.serviceinfos + i)->psid;
        (pps +i)->max_priority = 
            (serviceinfos.serviceinfos + i)->max_priority;

        (pps+i)->service_specific_permissions.len = 
            (serviceinfos.serviceinfos + i)->ssp.len;
        (pps +i)->service_specific_permissions.buf = 
            (u8*)malloc( (serviceinfos.serviceinfos+i)->ssp.len) ;
        if((pps + i)->service_specific_permissions.buf == NULL)
            goto fail;

        memcpy( (pps +i)->service_specific_permissions.buf,
                (serviceinfos.serviceinfos+i)->ssp.buf,(serviceinfos.serviceinfos+i)->ssp.len);
    }
    
    time(&now);
    pthread_mutex_lock(&cmdb->lock);
    expire  = now + cmdb->life_time*24*60*60;
    if(sec_get_certificate_request(sdb,CERTIFICATE,cmdb->ca_cmh,WSA,
                            IMPLICT,&permissions,&cmdb->identifier,&cmdb->region,
                           true,true,now,expire,veri_pk,enc_pk,res_pk,&cmdb->ca_cert,data,request_hash)){
        pthread_mutex_unlock(&cmdb->lock);
        wave_printf(MSG_ERROR,"cmp 获取证书请求失败 %s %d",__FILE__,__LINE__);
        goto fail;
    }

    lsis_array_free(&cmdb->req_cert_lsises);
    cmdb->req_cert_lsises.lsis = (pssme_lsis*)malloc(sizeof(pssme_lsis) * serviceinfos.len);
    if(cmdb->req_cert_lsises.lsis == NULL){
        pthread_mutex_unlock(&cmdb->lock);
        goto fail;
    }
    cmdb->req_cert_lsises.len = serviceinfos.len;
    for(i=0;i<serviceinfos.len;i++){
        *(cmdb->req_cert_lsises.lsis+i) = (serviceinfos.serviceinfos + i)->lsis;
    }
    pthread_mutex_lock(&cmdb->lock);
    cme_permissions_free(&permissions);
    serviceinfo_array_free(&serviceinfos);
    return 0;
fail:
    cme_permissions_free(&permissions);
    serviceinfo_array_free(&serviceinfos);
    return -1;
}
//现在这个函数的处理逻辑是我只负责申请所有服务的一个证书，用来签证书，当信道拥挤的情况没有考虑。
static void certificate_request_progress(struct cmp_db* cmdb,struct sec_db* sdb){
    cmh cert_cmh,key_pair_cmh;
    string cert_pk,keypair_pk;
    cme_lsis lsis = -1;
    string data;
    certid10 resquest_hash;
    int i;
    INIT(cert_pk);
    INIT(keypair_pk);
    INIT(data);
    INIT(resquest_hash);

    if(cme_cmh_request(sdb,&cert_cmh) || cme_cmh_request(sdb,&key_pair_cmh))
        goto fail;
    if(cme_generate_keypair(sdb,cert_cmh,ECDSA_NISTP256_WITH_SHA256,&cert_pk)||
            cme_generate_keypair(sdb,key_pair_cmh,ECIES_NISTP256,&keypair_pk))
        goto fail;
    if(generate_cert_request(sdb,cmdb,lsis,&cert_pk,NULL,&keypair_pk,&data,&resquest_hash))
        goto fail;
    ca_write(data.buf,data.len);
    pthread_mutex_lock(&cmdb->lock);
    cmdb->req_cert_cmh = cert_cmh;
    cmdb->req_cert_enc_cmh  = key_pair_cmh;
    pthread_mutex_unlock(&cmdb->lock);
    
    string_free(&cert_pk);
    string_free(&keypair_pk);
    string_free(&data);
    certid10_free(&resquest_hash);
    return;

fail:
    string_free(&cert_pk);
    string_free(&keypair_pk);
    string_free(&data);
    certid10_free(&resquest_hash);
    return ;
    
}
static void crl_request_progress(struct cmp_db* cmdb){
    sec_data sdata;
    crl_request* crl_req;
    u8* buf = NULL;
    u32 len = CRL_REQUSET_LEN;
    u32 larger = 1;
    u32 data_len;
    INIT(sdata);
    
    sdata.protocol_version = CURRETN_VERSION;
    sdata.type = CRL_REQUEST;
    crl_req = &sdata.u.crl_request;
    pthread_mutex_lock(&cmdb->lock);
    crl_req->crl_series = cmdb->crl_request_crl_series;
    crl_req->issue_date = cmdb->crl_request_issue_date;
    hashedid8_cpy(&crl_req->issuer,&cmdb->crl_request_issuer);
    pthread_mutex_unlock(&cmdb->lock);
    
    do{ 
        len = len*larger;
        if(buf != NULL)
            free(buf);
        buf = (u8*)malloc(len);
        if(buf == NULL)
            goto fail;
        larger++;
        data_len = crl_req_2_buf(buf,len,&crl_req);
    }while( data_len == NOT_ENOUGHT);

    ca_write(buf,data_len);
    sec_data_free(&sdata);
    free(buf);
    set_crl_request_alarm(cmdb);//发送了就设定下一个闹钟来请求
fail:
    sec_data_free(&sdata); 
    set_crl_request_alarm(cmdb);//发送了就设定下一个闹钟来请求
}
static void crl_recieve_progress(struct sec_db* sdb,struct cmp_db* cmdb,string* data){
    sec_data sdata;
    tobesigned_crl* unsigned_crl;
    struct crl_req_time *head,*ptr,*new;
    int i;

    INIT(sdata);

    if(sec_crl_verification(sdb,data,OVERDUE,NULL,
                NULL,NULL))
        goto fail;
    if( buf_2_sec_data(data->buf,data->len,&sdata))
        goto fail;
    
    unsigned_crl  = &sdata.u.crl.unsigned_crl;

    pthread_mutex_lock(&cmdb->lock);
    head = &cmdb->crl_time;
    list_for_each_entry(ptr,&head->list,list){
        if(ptr->crl_series == unsigned_crl->crl_series && 
                hashedid8_compare(&ptr->issuer,&unsigned_crl->ca_id) == 0)
            break;
    }
    if(&ptr->list == &head->list){
        new = (struct crl_req_time*)malloc(sizeof(struct crl_req_time));
        if(ptr == NULL)
            goto fail;
        new->crl_series = unsigned_crl->crl_series;
        new->time = unsigned_crl->next_crl;
        new->issue_date = unsigned_crl->issue_date;
        hashedid8_cpy(&new->issuer,&unsigned_crl->ca_id);
        crl_req_time_insert(cmdb,new); 
    }
    else{
        ptr->issue_date = unsigned_crl->issue_date;
        ptr->time = unsigned_crl->next_crl;
    }
    pthread_mutex_unlock(&cmdb->lock);

    if(unsigned_crl->type == ID_ONLY){
        for(i=0;i<unsigned_crl->u.entries.len;i++){
            cme_add_certificate_revocation(sdb,unsigned_crl->u.entries.buf + i,
                    &unsigned_crl->ca_id,unsigned_crl->crl_series,0);
        }
    }
    else if(unsigned_crl->type == ID_AND_EXPIRY){
        for(i=0;i<unsigned_crl->u.expiring_entries.len;i++){
            cme_add_certificate_revocation(sdb,
                    &( (unsigned_crl->u.expiring_entries.buf+i)->id),
                    &unsigned_crl->ca_id,unsigned_crl->crl_serial,
                     (unsigned_crl->u.expiring_entries.buf+i)->expiry);
        }
    }
    cme_add_crlinfo(sdb,unsigned_crl->type,unsigned_crl->crl_series,
                &unsigned_crl->ca_id,unsigned_crl->crl_serial,
                unsigned_crl->start_period,
                unsigned_crl->issue_date,
                unsigned_crl->next_crl);

    crl_free(&sdata);
fail:
    crl_free(&sdata);
}
static void cert_responce_recieve_progress(struct sec_db* sdb,struct cmp_db* cmdb,string* data){
    cmh cert_cmh,respon_cmh;
    content_type type;
    certificate certificate;
    string rec_value;
    bool ack_request;

    INIT(certificate);
    INIT(rec_value);

    pthread_mutex_lock(&cmdb->lock);
    respon_cmh = cmdb->req_cert_enc_cmh;
    cert_cmh = cmdb->req_cert_cmh;
    pthread_mutex_unlock(&cmdb->lock);
    if(cert_cmh == 0 || respon_cmh == 0)
        goto fail;
    if( sec_certificate_response_processing(sdb,respon_cmh,data,
            &type,NULL,NULL,&certificate,&rec_value,NULL) )
        goto fail;
    if(type != CERTIFICATE_RESPONSE)
        goto fail;
    /**
     * 这里设计到协议的transfor，这里我更本不知道是怎么做变换，所以我就假设它是不变的，
     * 就是rec_value.
     */
    if(cme_store_cert(sdb,cert_cmh,&certificate,&rec_value))
        goto fail;
    pthread_mutex_lock(&cmdb->lock);
    if(pssme_cryptomaterial_handle_storage(sdb,cert_cmh,&cmdb->req_cert_lsises)){
        pthread_mutex_unlock(&cmdb->lock);
        goto fail;
    }
    lsis_array_free(&cmdb->req_cert_lsises);
    cmdb->req_cert_cmh = 0;
    cmdb->req_cert_enc_cmh = 0;
    pthread_mutex_unlock(&cmdb->lock);

    certificate_free(&certificate);
    string_free(&rec_value);
    return ;

fail:
    certificate_free(&certificate);
    string_free(&rec_value);
    return ;
}
static void data_recieve_progress(struct sec_db* sdb,struct cmp_db* cmdb){
    sec_data sdata;
    string rec_data;
    int len =RECIEVE_DATA_MAXLEN;
    content_type inner_type;
    INIT(sdata);
    INIT(rec_data);
    
    do{
        if(rec_data.buf != NULL)
            free(rec_data.buf);
        rec_data.len = len;
        rec_data.buf = (u8*)malloc(len);
        if(rec_data.buf == NULL)
            goto fail;
        len = ca_try_read(rec_data.buf,rec_data.len);
    }while(len > rec_data.len);

    if(buf_2_sec_data(rec_data.buf,rec_data.len,&sdata))
        goto fail;
    if(sdata.protocol_version != CURRETN_VERSION)
        goto fail;


    pthread_mutext_lock(&cmdb->lock);

    /**这个地方的逻辑不知道有没有出错，这里按照协议一共三种情况
     * 1.1602dotdata的type 是encrtypted，tobeencrypted里面的type是certificate_response
     * 2.1602dotdata的type 是encrtypted，tobeencryptted里面的type是crl。
     * 3.1602dotdata的type是crl
     *
     * 可以看到情况考虑完，应该是上述三种情况分流出来 受到的数据是crl还是certificate_response
     * 但是读协议d4，我感觉第二中情况不是我这里处理的，或者我根本没有办法处理，因为我这里不会存储那个加密的钥匙或者证书
     */
    switch(sdata.type){
        case ENCRYPTED:
            //certificate_response
            if(!sec_secure_data_content_extration(sdb,&rec_data,cmdb->req_cert_enc_cmh,
                        NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL)){
                cert_responce_recieve_progress(sdb,cmdb,&rec_data);
            }
            break;
        case CRL:
            crl_recieve_progress(sdb,cmdb,&rec_data);
            break;
    }
    pthread_mutext_unlock(&cmdb->lock);
    
    sec_data_free(&sdata);
    string_free(&rec_data);
fail:
    sec_data_free(&sdata);
    string_free(&rec_data);
}
void cmp_do_crl_req(crl_series crl_series,hashedid8* issuer){
    struct crl_req_time *node;
    struct list_head *head;
    time_t now;
    time(&now);
    pthread_mutex_lock(&cmdb->lock);
    head = &cmdb->crl_time.list;
    list_for_each_entry(node,head,list){
        if(node->crl_series == crl_series){
            break;
        }
    }
    if(&node->list == head){
        node = (struct crl_req_time*)malloc(sizeof(struct crl_req_time));
        if(node == NULL){
            wave_error_printf("内存分配失败");
            pthread_mutex_unlock(&cmdb->lock);
            return ;
        }
        node->crl_series = crl_series;
        node->issue_date = 0;//代表要最近的
        node->time = now + 60;//加一分钟的偏移量
        hashedid8_cpy(&node->issuer,issuer);
        crl_req_time_insert(cmdb,node);
        pending_crl_request(cmdb);
    }
    pthread_mutex_unlock(&cmdb->lock);
}
void cmp_run(struct sec_db* sdb){
    while(1){
        pthread_mutex_lock(&cmdb->lock);
        while(cmdb->pending == 0)
            pthread_cond_wait(&pending_cond,&cmdb->lock);

        if(cmdb->pending & CRL_REQUEST ){
            crl_request_progress(cmdb);
            cmdb->pending &= ~CRL_REQUEST;
        }
        if(cmdb->pending & CERTFICATE_REQUEST){
            certificate_request_progress(cmdb,sdb);
            cmdb->pending &= ~CERTFICATE_REQUEST;
        }
        if(cmdb->pending & RECIEVE_DATA){
            data_recieve_progress(sdb,cmdb);
            cmdb->pending &= ~RECIEVE_DATA;
        }
        pthread_mutex_unlock(&cmdb->lock);
    }
    return;
}

