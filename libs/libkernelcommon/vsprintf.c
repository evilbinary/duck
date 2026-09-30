#include "stdarg.h"
#include "string.h"

/* we use this so that we can do without the ctype library */
#define is_digit(c) ((c) >= '0' && (c) <= '9')

#define ZEROPAD 1  /* pad with zero */
#define SIGN 2     /* unsigned/signed long */
#define PLUS 4     /* show plus */
#define SPACE 8    /* space if plus */
#define LEFT 16    /* left justified */
#define SPECIAL 32 /* 0x */
#define SMALL 64   /* use 'abcdef' instead of 'ABCDEF' */

// #define do_div(n,base) ({ \
// int __res; \
// __asm__("divl %4":"=a" (n),"=d" (__res):"0" (n),"1" (0),"r" (base)); \
// __res; })
#if defined(ARMV7) || defined(ARMV5)
#define do_div(n, base)                              \
  ({                                                 \
    unsigned int __rem = (unsigned int)(n) % (unsigned int)(base); \
    (n) = (unsigned int)(n) / (unsigned int)(base);  \
    __rem;                                           \
  })
#else
#define do_div(n, base)   \
  ({                      \
    unsigned long long __rem = n % base; \
    n /= base;            \
    __rem;                \
  })
#endif

/*------------------------------------------------------------------------
 Procedure:     skip_atoi ID:1
 Purpose:
 Input:
 Output:
 Errors:
------------------------------------------------------------------------*/
static int skip_atoi(const char **s) {
  int i = 0;

  while (is_digit(**s)) i = i * 10 + *((*s)++) - '0';
  return i;
}

/*------------------------------------------------------------------------
 Procedure:     number ID:1
 Purpose:
 Input:
 Output:
 Errors:
------------------------------------------------------------------------*/
static char *number(char *str, unsigned long long num, int base, int size,
                    int precision, int type) {
  // Guard against corrupted size from va_arg misread
  if (size > 64) size = 64;
  if (precision > 64) precision = 64;
  int i;
  char c, sign, tmp[36];
  const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  if (type & SMALL) digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (type & LEFT) type &= ~ZEROPAD;
  if (base < 2 || base > 36) return 0;
  c = (type & ZEROPAD) ? '0' : ' ';
  if (type & SIGN && (long long) num < 0) {
    sign = '-';
    num = -num;
  } else
    sign = (type & PLUS) ? '+' : ((type & SPACE) ? ' ' : 0);
  if (sign) size--;
  if (type & SPECIAL) {
    if (base == 16)
      size -= 2;
    else if (base == 8)
      size--;
  }
  i = 0;
  if (num == 0)
    tmp[i++] = '0';
  else
    while (num != 0) tmp[i++] = digits[do_div(num, base)];
  if (i > precision) precision = i;
  size -= precision;
  if (!(type & (ZEROPAD + LEFT)))
    while (size-- > 0) *str++ = ' ';
  if (sign) *str++ = sign;
  if (type & SPECIAL) {
    if (base == 8)
      *str++ = '0';
    else if (base == 16) {
      *str++ = '0';
      *str++ = digits[33];
    }
  }
  if (!(type & LEFT))
    while (size-- > 0) *str++ = c;
  while (i < precision--) *str++ = '0';
  while (i-- > 0) *str++ = tmp[i];
  while (size-- > 0) *str++ = ' ';
  return str;
}

static char *number32(char *str, unsigned int num, int base, int size,
                      int precision, int type) {
  if (size > 64) size = 64;
  if (precision > 64) precision = 64;
  int i;
  char c, sign, tmp[36];
  const char *digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  if (type & SMALL) digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (type & LEFT) type &= ~ZEROPAD;
  if (base < 2 || base > 36) return 0;
  c = (type & ZEROPAD) ? '0' : ' ';
  if (type & SIGN && (int)num < 0) {
    sign = '-';
    num = (unsigned int)(-(int)num);
  } else {
    sign = (type & PLUS) ? '+' : ((type & SPACE) ? ' ' : 0);
  }
  if (sign) size--;
  if (type & SPECIAL) {
    if (base == 16)
      size -= 2;
    else if (base == 8)
      size--;
  }
  i = 0;
  if (num == 0) {
    tmp[i++] = '0';
  } else {
    while (num != 0) {
      unsigned int rem = num % (unsigned int)base;
      num /= (unsigned int)base;
      tmp[i++] = digits[rem];
    }
  }
  if (i > precision) precision = i;
  size -= precision;
  if (!(type & (ZEROPAD + LEFT)))
    while (size-- > 0) *str++ = ' ';
  if (sign) *str++ = sign;
  if (type & SPECIAL) {
    if (base == 8)
      *str++ = '0';
    else if (base == 16) {
      *str++ = '0';
      *str++ = digits[33];
    }
  }
  if (!(type & LEFT))
    while (size-- > 0) *str++ = c;
  while (i < precision--) *str++ = '0';
  while (i-- > 0) *str++ = tmp[i];
  while (size-- > 0) *str++ = ' ';
  return str;
}

/*------------------------------------------------------------------------
 Procedure:     kvsprintf ID:1
 Purpose:
 Input:
 Output:
 Errors:
------------------------------------------------------------------------*/
/*------------------------------------------------------------------------
 Procedure:     kvsnprintf ID:1
 Purpose:       带界格式化（vsnprintf 语义）
 Input:         buf/size: 缓冲区与容量（含结尾 '\0'）；size==0 时不写任何字节
 Output:        实际写入的字符数（不含结尾 '\0'）
 Errors:        绝不写出 buf[0..size-1]
------------------------------------------------------------------------*/
/* 【边界策略】不改动 number()/number32() 本体，只在调用处按"剩余空间"夹紧：
 *   - 数值转换：把 field_width / precision 夹到 room-5（room = 剩余字节），
 *     因 number() 输出 ≤ max(field_width, precision) + 4，故单次转换 ≤ room-1 字节；
 *   - %s：字符串长度夹到 room-1；
 *   - %c/%s 的填充宽度夹到 64（与 number() 内部对 size 的上限一致）；
 *   - 格式串里的字面字符逐个检查剩余空间（格式串本身可能比缓冲区长）。
 * 关键点：判据必须基于"剩余空间"，不能用固定预留值 —— 小缓冲区会因此输出空串。
 * 为什么需要它：kprintf 的 2KB 缓冲是所有 CPU 共用的，格式化结果超长就会踩掉紧邻的
 * print_lock（实测两符号地址相距正好 2048 = KPRINT_BUF），打印锁一卡整机日志全停。 */
extern void print_char(u8 ch); /* 【bring-up 临时探针】 */

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list *args) {
  int len;
  int i;
  char *str;
  char *s;
  int *ip;
  int flags;       /* flags to number() */
  int field_width; /* width of output field */
  int precision; /* min. # of digits for integers; max number of chars for from
                    string */
  int qualifier; /* 'h', 'l', or 'L' for integer fields */

  unsigned long long num;

  for (str = buf; *fmt; fmt++) {
    if (*fmt != '%') {
      /* 【边界】字面字符也要检查：格式串本身可能比缓冲区长 */
      if (size == 0 || (size_t)(str - buf) + 1 >= size) {
        break;
      }
      *str++ = *fmt;
      continue;
    }

    /* process flags */
    flags = 0;
  repeat:
    ++fmt; /* this also skips first '%' */
    switch (*fmt) {
      case '-':
        flags |= LEFT;
        goto repeat;
      case '+':
        flags |= PLUS;
        goto repeat;
      case ' ':
        flags |= SPACE;
        goto repeat;
      case '#':
        flags |= SPECIAL;
        goto repeat;
      case '0':
        flags |= ZEROPAD;
        goto repeat;
    }

    /* get field width */
    field_width = -1;
    if (is_digit(*fmt))
      field_width = skip_atoi(&fmt);
    else if (*fmt == '*') {
      /* it's the next argument */
      field_width = va_arg(*args, int);
      if (field_width < 0) {
        field_width = -field_width;
        flags |= LEFT;
      }
    }

    /* 【边界】夹住宽度：%c/%s 的填充循环按 field_width 逐字符写，
     * 不夹的话 `%100000d` 这类格式能把缓冲区直接写穿。64 与 number() 的上限一致。 */
    if (field_width > 64) {
      field_width = 64;
    }

    /* get the precision */
    precision = -1;
    if (*fmt == '.') {
      ++fmt;
      if (is_digit(*fmt))
        precision = skip_atoi(&fmt);
      else if (*fmt == '*') {
        /* it's the next argument */
        precision = va_arg(*args, int);
      }
      if (precision < 0) precision = 0;
    }

    /* get the conversion qualifier */
    qualifier = -1;
    if (*fmt == 'h' || *fmt == 'l' || *fmt == 'L') {
      qualifier = *fmt;
      ++fmt;
      /* handle 'll' (long long) */
      if (qualifier == 'l' && *fmt == 'l') {
        qualifier = 'L';  /* use 'L' to represent 'll' */
        ++fmt;
      }
    }

    /* 【边界】把"这一次转换可能写出的量"夹到剩余空间内：
     *   number()/number32() 的输出 ≤ max(field_width, precision) + 4（符号 + "0x" 前缀），
     *   而它们内部已把 size/precision 各夹到 64 ⇒ 这里再把两者夹到 room-5，
     *   单次转换最多写 room-1 字节，给结尾 '\0' 留出 1 字节，绝不越界。
     * 【不能用"固定预留 N 字节"的判据】缓冲区本身小于 N 时会造成"一个字符都写不出去"
     * （实测 64 字节缓冲 + 预留 160 ⇒ 直接输出空串 ✗）。 */
    {
      size_t used = (size_t)(str - buf);
      size_t room = (size > used) ? (size - used) : 0;
      int cap;
      if (room < 6) {
        break;
      }
      cap = (int)room - 5;
      if (field_width > cap) {
        field_width = cap;
      }
      if (precision > cap) {
        precision = cap;
      }
    }

    switch (*fmt) {
      case 'c':
        if (!(flags & LEFT))
          while (--field_width > 0) *str++ = ' ';
        *str++ = (unsigned char)va_arg(*args, int);
        while (--field_width > 0) *str++ = ' ';
        break;

      case 's':
        s = va_arg(*args, char *);
        len = kstrlen(s);
        if (precision < 0)
          precision = len;
        else if (len > precision)
          len = precision;
        /* 【边界】%s 的长度由调用者字符串决定，可以任意长 ⇒ 按剩余空间夹紧 */
        {
          size_t used2 = (size_t)(str - buf);
          size_t remain = (size > used2) ? (size - used2) : 0;
          if (remain == 0 || (size_t)len > remain - 1) {
            len = (remain == 0) ? 0 : (int)(remain - 1);
          }
        }

        if (!(flags & LEFT))
          while (len < field_width--) *str++ = ' ';
        for (i = 0; i < len; ++i) *str++ = *s++;
        while (len < field_width--) *str++ = ' ';
        break;

      case 'o':
        if (qualifier == 'L') {
          str = number(str, va_arg(*args, unsigned long long), 8, field_width,
                       precision, flags);
        } else if (qualifier == 'l') {
          str = number32(str, va_arg(*args, unsigned long), 8, field_width,
                         precision, flags);
        } else {
          str = number32(str, va_arg(*args, unsigned int), 8, field_width,
                         precision, flags);
        }
        break;

      case 'p':
        if (field_width == -1) {
          field_width = 16;   // ARM64: 64-bit pointer = 16 hex chars
          flags |= ZEROPAD;
        }
        str = number(str, (unsigned long long)(uintptr_t)va_arg(*args, void *),
                     16, field_width, precision, flags);
        break;

      case 'x':
        flags |= SMALL;
        // fall through
      case 'X':
        if (qualifier == 'L') {
          str = number(str, va_arg(*args, unsigned long long), 16, field_width,
                       precision, flags);
        } else if (qualifier == 'l') {
          str = number32(str, va_arg(*args, unsigned long), 16, field_width,
                         precision, flags);
        } else {
          str = number32(str, va_arg(*args, unsigned int), 16, field_width,
                         precision, flags);
        }
        break;

      case 'd':
      case 'i':
        flags |= SIGN;
        // fall through
      case 'u':
        if (qualifier == 'L') {
          num = va_arg(*args, unsigned long long);
          if (flags & SIGN) num = (long long)num;
          str = number(str, num, 10, field_width, precision, flags);
        } else if (qualifier == 'l') {
          unsigned int num32 = va_arg(*args, unsigned long);
          str = number32(str, num32, 10, field_width, precision, flags);
        } else {
          unsigned int num32 = va_arg(*args, unsigned int);
          str = number32(str, num32, 10, field_width, precision, flags);
        }
        break;

      case 'n':
        ip = va_arg(*args, int *);
        *ip = (str - buf);
        break;

      default:
        if (*fmt != '%') *str++ = '%';
        if (*fmt)
          *str++ = *fmt;
        else
          --fmt;
        break;
    }
  }
  /* 【结尾】保证一定有 '\0'：写指针理论上可能已经推到缓冲区末尾（截断时）。 */
  if (size > 0) {
    size_t used = (size_t)(str - buf);
    buf[(used < size) ? used : (size - 1)] = '\0';
  }
  return str - buf;
}

/* 【无界包装】保留原语义：logger.c / serial 模块 / sprintf 等老调用者行为完全不变。
 * 新代码请优先用 kvsnprintf() 并把缓冲容量传进来。 */
#define KVSPRINTF_UNBOUNDED ((size_t)0x40000000) /* 1GB：远大于任何真实缓冲区 */

int kvsprintf(char *buf, const char *fmt, va_list *args) {
  return kvsnprintf(buf, KVSPRINTF_UNBOUNDED, fmt, args);
}

/* 带界 + 不定参的便捷包装：给"直接写代码调用"的场景用（内部走 va_list 版本）。
 * 老代码请继续用 kvsprintf（无界），新代码优先用 ksnprintf/kvsnprintf。 */
int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
  va_list args;
  int r;
  va_start(args, fmt);
  r = kvsnprintf(buf, size, fmt, &args);
  va_end(args);
  return r;
}

void sprintf(char *buf, const char *fmt, ...) {
  int i;
  va_list args;
  va_start(args, fmt);
  i = kvsprintf(buf, fmt, &args);
  va_end(args);
}