/*******************************************************************
 * Copyright 2021-present evilbinary
 * 作者: evilbinary on 01/01/20
 * 邮箱: rootdebug@163.com
 ********************************************************************/
#include "type.h"

// 格里高利历中每个月的天数（非闰年）
static const int days_per_month[12] = {31, 28, 31, 30, 31, 30,
                                       31, 31, 30, 31, 30, 31};

// 判断是否为闰年
int is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 秒数转换为日期和时间的函数
void seconds_to_date(int seconds, int *year, int *month, int *day, int *hour,
                     int *minute, int *second) {
  *second = seconds % 60;
  seconds /= 60;  // 转换为分钟
  *minute = seconds % 60;
  seconds /= 60;  // 转换为小时
  *hour = seconds % 24;
  seconds /= 24;  // 转换为天数

  // 计算年
  *year = 1970;  // Unix时间戳的起始年
  while (seconds >= 365) {
    if (is_leap_year((*year) + 1970)) {
      if (seconds >= 366) {
        seconds -= 366;
      } else {
        break;
      }
    } else {
      seconds -= 365;
    }
    (*year)++;
  }

  // 计算月
  *month = 0;
  for (int i = 0; i < 12; i++) {
    int days_in_month = is_leap_year(*year) && i == 1 ? 29 : days_per_month[i];
    if (seconds < days_in_month) {
      *month = i + 1;
      break;
    }
    seconds -= days_in_month;
  }

  // 计算日
  *day = seconds + 1;  // 秒数转换为天数，从1开始计数
}

// 天数转换为年月日
void days_to_date(int days, int *year, int *month, int *day) {
  *year = 1900;  // 开始年份
  *month = 1;    // 开始月份
  *day = 1;      // 开始日

  if (days < 30) {
    return;
  }

  while (days > 0) {
    // 如果是闰年，二月有29天，否则有28天
    if (is_leap_year(*year)) {
      if (days > 366) {
        days -= 366;
        (*year)++;
      } else {
        *month = 3;
        *day = days - 31;  // 1月31天，2月29天
        days = 0;
      }
    } else {
      // 非闰年的二月有28天
      if (days > days_per_month[*month - 1]) {
        days -= days_per_month[*month - 1];
        (*month)++;
      } else {
        *day = days;
        days = 0;
      }
    }
  }
}


uint32_t secs_of_years(int years) {
  uint32_t days = 0;
  years += 2000;
  while (years > 1969) {
    days += 365;
    if (years % 4 == 0) {
      if (years % 100 == 0) {
        if (years % 400 == 0) {
          days++;
        }
      } else {
        days++;
      }
    }
    years--;
  }
  return days * 86400;
}

uint32_t secs_of_month(int months, int year) {
  // year += 2000;

  uint32_t days = 0;
  switch (months) {
    case 11:
      days += 30;
    case 10:
      days += 31;
    case 9:
      days += 30;
    case 8:
      days += 31;
    case 7:
      days += 31;
    case 6:
      days += 30;
    case 5:
      days += 31;
    case 4:
      days += 30;
    case 3:
      days += 31;
    case 2:
      days += 28;
      if ((year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0))) {
        days++;
      }
    case 1:
      days += 31;
    default:
      break;
  }
  return days * 86400;
}