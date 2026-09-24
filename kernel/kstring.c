#include "kstring.h"

void serial_write_public(const char *text);

void *memcpy(void *destination, const void *source, zeroos_size_t length) {
    uint8_t *d=(uint8_t *)destination;
    const uint8_t *s=(const uint8_t *)source;
    if ((((uint64_t)d|(uint64_t)s|length)&7ULL)==0) {
        uint64_t *dw=(uint64_t *)destination;
        const uint64_t *sw=(const uint64_t *)source;
        for (zeroos_size_t i=0; i<length/8; ++i)
            dw[i]=sw[i];
        return destination;
    }
    for (zeroos_size_t i=0; i<length; ++i)
        d[i]=s[i];
    return destination;
}

void *memmove(void *destination, const void *source, zeroos_size_t length) {
    uint8_t *d=(uint8_t *)destination;
    const uint8_t *s=(const uint8_t *)source;
    if (d==s || length==0)
        return destination;
    if (d<s || d>=s+length)
        return memcpy(destination,source,length);
    while (length--)
        d[length]=s[length];
    return destination;
}

void *memset(void *destination, int value, zeroos_size_t length) {
    uint8_t *d=(uint8_t *)destination;
    if (((((uint64_t)d)|length)&7ULL)==0) {
        uint64_t pattern=(uint8_t)value;
        pattern|=pattern<<8; pattern|=pattern<<16; pattern|=pattern<<32;
        uint64_t *dw=(uint64_t *)destination;
        for (zeroos_size_t i=0; i<length/8; ++i)
            dw[i]=pattern;
        return destination;
    }
    for (zeroos_size_t i=0; i<length; ++i)
        d[i]=(uint8_t)value;
    return destination;
}

int memcmp(const void *left, const void *right, zeroos_size_t length) {
    const uint8_t *l=(const uint8_t *)left;
    const uint8_t *r=(const uint8_t *)right;
    for (zeroos_size_t i=0; i<length; ++i)
        if (l[i]!=r[i])
            return l[i]<r[i] ? -1 : 1;
    return 0;
}

uint64_t kstrnlen(const char *text, uint64_t limit) {
    uint64_t length=0;
    while (length<limit && text[length])
        ++length;
    return length;
}

int kstrneq(const char *left, const char *right, uint64_t limit) {
    for (uint64_t i=0; i<limit; ++i) {
        if (left[i]!=right[i])
            return 0;
        if (!left[i])
            return 1;
    }
    return 1;
}

struct kfmt_out {
    char *buffer;
    uint64_t size;
    uint64_t used;
};

static void kfmt_putc(struct kfmt_out *out, char c) {
    if (out->used+1<out->size)
        out->buffer[out->used]=c;
    ++out->used;
}

static void kfmt_number(struct kfmt_out *out, uint64_t value, unsigned base,
                        int negative, int width, char pad) {
    char digits[24];
    int count=0;
    do {
        unsigned digit=(unsigned)(value%base);
        digits[count++]=(char)(digit<10 ? '0'+digit : 'a'+digit-10);
        value/=base;
    } while (value && count<(int)sizeof(digits));
    if (negative)
        kfmt_putc(out,'-');
    for (int i=count+(negative?1:0); i<width; ++i)
        kfmt_putc(out,pad);
    while (count)
        kfmt_putc(out,digits[--count]);
}

static uint64_t kvsnprintf(char *buffer, uint64_t size, const char *format,
                           __builtin_va_list args) {
    struct kfmt_out out={buffer,size,0};
    if (!buffer || size==0)
        return 0;
    while (*format) {
        char c=*format++;
        int width=0;
        char pad=' ';
        int longness=0;
        if (c!='%') {
            kfmt_putc(&out,c);
            continue;
        }
        if (*format=='0') {
            pad='0';
            ++format;
        }
        while (*format>='0' && *format<='9')
            width=width*10+(*format++-'0');
        while (*format=='l') {
            ++longness;
            ++format;
        }
        c=*format++;
        switch (c) {
        case 's': {
            const char *text=__builtin_va_arg(args,const char *);
            if (!text)
                text="(null)";
            while (*text)
                kfmt_putc(&out,*text++);
            break;
        }
        case 'c':
            kfmt_putc(&out,(char)__builtin_va_arg(args,int));
            break;
        case 'd': {
            int64_t value=longness ? __builtin_va_arg(args,int64_t)
                                   : (int64_t)__builtin_va_arg(args,int);
            kfmt_number(&out,value<0 ? (uint64_t)(-value) : (uint64_t)value,
                        10,value<0,width,pad);
            break;
        }
        case 'u':
            kfmt_number(&out,longness ? __builtin_va_arg(args,uint64_t)
                                      : __builtin_va_arg(args,unsigned),
                        10,0,width,pad);
            break;
        case 'x':
            kfmt_number(&out,longness ? __builtin_va_arg(args,uint64_t)
                                      : __builtin_va_arg(args,unsigned),
                        16,0,width,pad);
            break;
        case 'p':
            kfmt_putc(&out,'0');
            kfmt_putc(&out,'x');
            kfmt_number(&out,(uint64_t)__builtin_va_arg(args,void *),16,0,0,' ');
            break;
        case '%':
            kfmt_putc(&out,'%');
            break;
        default:
            kfmt_putc(&out,'?');
            break;
        }
        if (!c)
            break;
    }
    buffer[out.used<size ? out.used : size-1]='\0';
    return out.used<size ? out.used : size-1;
}

uint64_t ksnprintf(char *buffer, uint64_t size, const char *format, ...) {
    __builtin_va_list args;
    __builtin_va_start(args,format);
    uint64_t written=kvsnprintf(buffer,size,format,args);
    __builtin_va_end(args);
    return written;
}

void klog(const char *format, ...) {
    char line[256];
    __builtin_va_list args;
    __builtin_va_start(args,format);
    uint64_t written=kvsnprintf(line,sizeof(line)-1,format,args);
    __builtin_va_end(args);
    if (written==0 || line[written-1]!='\n') {
        line[written]='\n';
        line[written+1]='\0';
    }
    serial_write_public(line);
}
