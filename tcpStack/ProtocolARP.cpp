//----------------------------------------------------------------------------
// Copyright( c ) 2015, Robert Kimball
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//----------------------------------------------------------------------------

#include <stdio.h>

#include "Address.h"
#include "Utility.h"
#include "ProtocolMACEthernet.h"
#include "ProtocolARP.h"
#include "ProtocolIP.h"
#include "DataBuffer.h"
#include "NetworkInterface.h"
#include "Config.h"

ARPCacheEntry ProtocolARP::Cache[ ProtocolARP::CacheSize ];

DataBuffer ProtocolARP::ARPRequest;

// HardwareType - 2 bytes
// ProtocolType - 2 bytes
// HardwareSize - 1 byte, size int bytes of HardwareAddress fields
// IPv4AddressSize - 1 byte, size int bytes of ProtocolAddress fields
// Op - 2 bytes
// SenderHardwareAddress - HardwareSize bytes
// SenderProtocolAddress - IPv4AddressSize bytes
// TargetHardwareAddress - HardwareSize bytes
// TargetProtocolAddress - IPv4AddressSize bytes

#define HardwareSizeOffset 4
#define ProtocolSizeOffset 5
#define OpOffset 6
#define SenderHardwareAddressOffset 8
#define SenderProtocolAddressOffset (SenderHardwareAddressOffset+ProtocolMACEthernet::GetAddressSize())
#define TargetHardwareAddressOffset (SenderProtocolAddressOffset+IPv4AddressSize)
#define TargetProtocolAddressOffset (TargetHardwareAddressOffset+ProtocolMACEthernet::GetAddressSize())

//============================================================================
//
//============================================================================

void ProtocolARP::Initialize()
{

}

//============================================================================
//
//============================================================================

void ProtocolARP::ProcessRx( DataBuffer* buffer )
{
   uint8_t targetProtocolOffset;
   uint16_t opType;
   uint8_t* packet = buffer->Packet;
   uint16_t length = buffer->Length;

   opType = Unpack16( packet, OpOffset );

   if( opType == 1 )
   {
      // ARP Request
      if( packet[ HardwareSizeOffset ] == ProtocolMACEthernet::GetAddressSize() &&
          packet[ ProtocolSizeOffset ] == IPv4AddressSize )
      {
         // All of the sizes match
         targetProtocolOffset = SenderHardwareAddressOffset + ProtocolMACEthernet::GetAddressSize() * 2 + IPv4AddressSize;

         if( Address::Compare( &packet[ targetProtocolOffset ], Config.IPv4.Address, IPv4AddressSize ) )
         {
            // This ARP is for me
            SendReply( packet, length );
         }
      }
   }
   else if( opType == 2 )
   {
      // ARP Reply
      Add( &packet[ SenderHardwareAddressOffset + ProtocolMACEthernet::GetAddressSize() ], &packet[ SenderHardwareAddressOffset ] );
      ProtocolIP::Retry();
   }
}

//============================================================================
//
//============================================================================

void ProtocolARP::Add( const uint8_t* protocolAddress, const uint8_t* hardwareAddress )
{
   int index;
   int i;
   int j;
   int oldest;

   index = LocateProtocolAddress( protocolAddress );
   if( index >= 0 )
   {
      // Found entry in table, reset it's age
      Cache[ index ].Age = 1;
   }
   else
   {
      // Not already in table;
      for( i = 0; i < CacheSize; i++ )
      {
         if( Cache[ i ].Age == 0 )
         {
            // Entry not used, take it
            break;
         }
      }
      if( i == CacheSize )
      {
         // Table is full, steal the oldest entry
         oldest = 0;
         for( i = 1; i<CacheSize; i++ )
         {
            if( Cache[ i ].Age > Cache[ oldest ].Age )
            {
               oldest = i;
            }
         }
         i = oldest;
      }

      // At this point i is the entry we want to use
      Cache[ i ].Age = 1;
      for( j = 0; j < IPv4AddressSize; j++ )
      {
         Cache[ i ].IPv4Address[ j ] = protocolAddress[ j ];
      }
      for( j = 0; j < ProtocolMACEthernet::GetAddressSize(); j++ )
      {
         Cache[ i ].MACAddress[ j ] = hardwareAddress[ j ];
      }
   }

   // Age the list
   for( i = 0; i < CacheSize; i++ )
   {
      if( Cache[ i ].Age != 0 )
      {
         Cache[ i ].Age++;
         if( Cache[ i ].Age == 0xFF )
         {
            // Flush this entry
            Cache[ i ].Age = 0;
         }
      }
   }
}

//============================================================================
//
//============================================================================

void ProtocolARP::Show( osPrintfInterface* pfunc )
{
   int i;

   pfunc->Printf( "ARP Cache:\n" );
   for( i = 0; i < CacheSize; i++ )
   {
      int length = pfunc->Printf( "   %d.%d.%d.%d ",
         Cache[ i ].IPv4Address[ 0 ],
         Cache[ i ].IPv4Address[ 1 ],
         Cache[ i ].IPv4Address[ 2 ],
         Cache[ i ].IPv4Address[ 3 ] );
      for( ; length<19; length++ ) pfunc->Printf( " " );
      pfunc->Printf( " %02X:%02X:%02X:%02X:%02X:%02X ",
         Cache[ i ].MACAddress[ 0 ],
         Cache[ i ].MACAddress[ 1 ],
         Cache[ i ].MACAddress[ 2 ],
         Cache[ i ].MACAddress[ 3 ],
         Cache[ i ].MACAddress[ 4 ],
         Cache[ i ].MACAddress[ 5 ] );
      pfunc->Printf( "   age = %d\n", Cache[ i ].Age );
   }
}

//============================================================================
//
//============================================================================

void ProtocolARP::SendReply( uint8_t* packet, int length )
{
   uint8_t i;
   DataBuffer* txBuffer;

   txBuffer = ProtocolMACEthernet::GetTxBuffer();

   if( txBuffer == 0 )
   {
      printf( "ARP failed to get tx buffer\n" );
      return;
   }

   txBuffer->Length += SenderHardwareAddressOffset + ProtocolMACEthernet::GetAddressSize() * 2 + IPv4AddressSize * 2;

   for( i = 0; i < OpOffset; i++ )
   {
      txBuffer->Packet[ i ] = packet[ i ];
   }

   // Set op field to be ARP Reply, which is '2'
   txBuffer->Packet[ OpOffset ] = 0;
   txBuffer->Packet[ OpOffset + 1 ] = 2;

   // Copy sender address to target address
   for( i = 0; i < ProtocolMACEthernet::GetAddressSize(); i++ )
   {
      txBuffer->Packet[ SenderHardwareAddressOffset + i ] = ProtocolMACEthernet::GetUnicastAddress()[ i ];
      txBuffer->Packet[ TargetHardwareAddressOffset + i ] = packet[ SenderHardwareAddressOffset + i ];
   }

   for( i = 0; i < IPv4AddressSize; i++ )
   {
      txBuffer->Packet[ SenderProtocolAddressOffset + i ] = Config.IPv4.Address[ i ];
      txBuffer->Packet[ TargetProtocolAddressOffset + i ] = packet[ SenderProtocolAddressOffset + i ];
   }

   ProtocolMACEthernet::Transmit( txBuffer, &packet[ SenderHardwareAddressOffset ], 0x0806 );
}

//============================================================================
//
//============================================================================

void ProtocolARP::SendRequest( const uint8_t* targetIP )
{
   ARPRequest.Initialize();
   ARPRequest.Packet += ProtocolMACEthernet::HeaderSize();
   ARPRequest.Remainder -= ProtocolMACEthernet::HeaderSize();

   ARPRequest.Length += SenderHardwareAddressOffset + ProtocolMACEthernet::GetAddressSize() * 2 + IPv4AddressSize * 2;
   ARPRequest.Disposable = false;

   size_t offset = 0;
   offset = Pack16( ARPRequest.Packet, offset, 0x0001 ); // Hardware Type
   offset = Pack16( ARPRequest.Packet, offset, 0x0800 ); // Protocol Type
   offset = Pack8( ARPRequest.Packet, offset, 6 ); // Hardware Size
   offset = Pack8( ARPRequest.Packet, offset, 4 ); // Protocol Size
   offset = Pack16( ARPRequest.Packet, offset, 0x0001 ); // Op

   // Sender's Hardware Address
   offset = PackBytes( ARPRequest.Packet, offset, ProtocolMACEthernet::GetUnicastAddress(), 6 );

   // Sender's Protocol Address
   offset = PackBytes( ARPRequest.Packet, offset, Config.IPv4.Address, 4 );

   // Target's Hardware Address
   ARPRequest.Packet[ 18 ] = 0;
   ARPRequest.Packet[ 19 ] = 0;
   ARPRequest.Packet[ 20 ] = 0;
   ARPRequest.Packet[ 21 ] = 0;
   ARPRequest.Packet[ 22 ] = 0;
   ARPRequest.Packet[ 23 ] = 0;

   // Target's Protocol Address
   PackBytes( ARPRequest.Packet, 24, targetIP, 4 );

   ProtocolMACEthernet::Transmit( &ARPRequest, ProtocolMACEthernet::GetBroadcastAddress(), 0x0806 );
}

//============================================================================
//
//============================================================================

uint8_t* ProtocolARP::Protocol2Hardware( const uint8_t* protocolAddress )
{
   int index;
   uint8_t* rc = 0;

   if( IsBroadcast( protocolAddress ) )
   {
      rc = ProtocolMACEthernet::GetBroadcastAddress();
   }
   else
   {
      if( !IsLocal( protocolAddress ) )
      {
         protocolAddress = Config.IPv4.Gateway;
      }
      index = LocateProtocolAddress( protocolAddress );

      if( index != -1 )
      {
         rc = Cache[ index ].MACAddress;
      }
      else
      {
         SendRequest( protocolAddress );
      }
   }
   return rc;
}

//============================================================================
//
//============================================================================

bool ProtocolARP::IsBroadcast( const uint8_t* protocolAddress )
{
   bool rc = true;
   for( int i = 0; i < IPv4AddressSize; i++ )
   {
      if( protocolAddress[ i ] != 0xFF )
      {
         rc = false;
         break;
      }
   }
   return rc;
}

//============================================================================
//
//============================================================================

bool ProtocolARP::IsLocal( const uint8_t* protocolAddress )
{
   int i;

   for( i = 0; i < IPv4AddressSize; i++ )
   {
      if
         (
            (protocolAddress[ i ] & Config.IPv4.SubnetMask[ i ]) !=
            (Config.IPv4.Address[ i ] & Config.IPv4.SubnetMask[ i ])
            )
      {
         break;
      }
   }

   return i == IPv4AddressSize;
}

//============================================================================
//
//============================================================================

int ProtocolARP::LocateProtocolAddress( const uint8_t* protocolAddress )
{
   int i;
   int j;

   for( i = 0; i < CacheSize; i++ )
   {
      // Go through the address backwards since least significant byte is most
      // likely to be unique
      for( j = IPv4AddressSize - 1; j >= 0; j-- )
      {
         if( Cache[ i ].IPv4Address[ j ] != protocolAddress[ j ] )
         {
            break;
         }
      }
      if( j == -1 )
      {
         // found
         return i;
      }
   }

   return -1;
}
