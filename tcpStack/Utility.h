//----------------------------------------------------------------------------
//  Copyright 2015 Robert Kimball
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//  http ://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.
//----------------------------------------------------------------------------

#ifndef UTILITY_H
#define UTILITY_H

#include <cinttypes>

typedef int (*PrintfFunctionPtr)( const char* fmt, ... );

#ifdef __cplusplus
extern "C"
{
#endif

void DumpData( void* buffer, int len, PrintfFunctionPtr );

void DumpBits( void* buffer, int size, PrintfFunctionPtr );

#ifdef __cplusplus
}
#endif

#endif
