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

#include <stdio.h>

#include "Address.h"
#include "Utility.h"
#include "ProtocolMACEthernet.h"
#include "ProtocolARP.h"
#include "ProtocolIP.h"
#include "DataBuffer.h"
#include "NetworkInterface.h"

extern AddressConfiguration Config;

ARPCacheEntry ProtocolARP::Cache[ ProtocolARP::CacheSize ];

DataBuffer ProtocolARP::ARPRequest;

// HardwareType - 2 bytes
// ProtocolType - 2 bytes
// HardwareSize - 1 byte, size int bytes of HardwareAddress fields
// ProtocolSize - 1 byte, size int bytes of ProtocolAddress fields
// Op - 2 bytes
// SenderHardwareAddress - HardwareSize bytes
// SenderProtocolAddress - ProtocolSize bytes
// TargetHardwareAddress - HardwareSize bytes
// TargetProtocolAddress - ProtocolSize bytes

#define HardwareSizeOffset 4
#define ProtocolSizeOffset 5
#define OpOffset 6
#define SenderHardwareAddressOffset 8
#define SenderProtocolAddressOffset (SenderHardwareAddressOffset+Address::HardwareSize)
#define TargetHardwareAddressOffset (SenderProtocolAddressOffset+Address::ProtocolSize)
#define TargetProtocolAddressOffset (TargetHardwareAddressOffset+Address::HardwareSize)

//============================================================================
//
//============================================================================

void ProtocolARP::ProcessRx( DataBuffer* buffer )
{
   uint8_t targetProtocolOffset;
   uint16_t opType;
   uint8_t* packet = buffer->Packet;
   uint16_t length = buffer->Length;

   opType = packet[ OpOffset ] << 8 | packet[ OpOffset+1 ];

   if( opType == 1 )
   {
      // ARP Request
      if( packet[ HardwareSizeOffset ] == Config.Address.HardwareSize && packet[ ProtocolSizeOffset ] == Config.Address.ProtocolSize )
      {
         // All of the sizes match
         targetProtocolOffset = SenderHardwareAddressOffset + Config.Address.HardwareSize*2 + Config.Address.ProtocolSize;

         if( Address::Compare( &packet[ targetProtocolOffset ], Config.Address.Protocol, Config.Address.ProtocolSize ) )
         {
            // This ARP is for me
            SendReply( packet, length );
         }
      }
   }
   else if( opType == 2 )
   {
      // ARP Reply
      Add( &packet[ SenderHardwareAddressOffset+Address::HardwareSize ], &packet[ SenderHardwareAddressOffset ] );
      ProtocolIP::Retry();
   }
}

//============================================================================
//
//============================================================================

void ProtocolARP::Add( uint8_t* protocolAddress, uint8_t* hardwareAddress )
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
      for( i=0; i<CacheSize; i++ )
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
         for( i=1; i<CacheSize; i++ )
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
      for( j=0; j<Address::ProtocolSize; j++ )
      {
         Cache[ i ].Protocol[ j ] = protocolAddress[ j ];
      }
      for( j=0; j<Address::HardwareSize; j++ )
      {
         Cache[ i ].Hardware[ j ] = hardwareAddress[ j ];
      }
   }

   // Age the list
   for( i=0; i<CacheSize; i++ )
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

void ProtocolARP::Show()
{
   int i;

   printf( "ARP Cache:\n" );
   for( i=0; i<CacheSize; i++ )
   {
      printf( "%3d.%3d.%3d.%3d ",
         Cache[ i ].Protocol[ 0 ],
         Cache[ i ].Protocol[ 1 ],
         Cache[ i ].Protocol[ 2 ],
         Cache[ i ].Protocol[ 3 ] );
      printf( "%02X:%02X:%02X:%02X:%02X:%02X ",
         Cache[ i ].Hardware[ 0 ],
         Cache[ i ].Hardware[ 1 ],
         Cache[ i ].Hardware[ 2 ],
         Cache[ i ].Hardware[ 3 ],
         Cache[ i ].Hardware[ 4 ],
         Cache[ i ].Hardware[ 5 ] );
      printf( "%d\n", Cache[ i ].Age );
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

   txBuffer->Length += SenderHardwareAddressOffset + Config.Address.HardwareSize*2 + Config.Address.ProtocolSize*2;

   for( i=0; i<OpOffset; i++ )
   {
      txBuffer->Packet[ i ] = packet[ i ];
   }

   // Set op field to be ARP Reply, which is '2'
   txBuffer->Packet[ OpOffset ]   = 0;
   txBuffer->Packet[ OpOffset+1 ] = 2;
   
   // Copy sender address to target address
   for( i=0; i<Config.Address.HardwareSize; i++ )
   {
      txBuffer->Packet[ SenderHardwareAddressOffset+i ] = Config.Address.Hardware[ i ];
      txBuffer->Packet[ TargetHardwareAddressOffset+i ] = packet[ SenderHardwareAddressOffset+i ];
   }

   for( i=0; i<Config.Address.ProtocolSize; i++ )
   {
      txBuffer->Packet[ SenderProtocolAddressOffset+i ] = Config.Address.Protocol[ i ];
      txBuffer->Packet[ TargetProtocolAddressOffset+i ] = packet[ SenderProtocolAddressOffset+i ];
   }

   ProtocolMACEthernet::Transmit( txBuffer, &packet[ SenderHardwareAddressOffset ], 0x0806 );
}

//============================================================================
//
//============================================================================

void ProtocolARP::SendRequest( uint8_t* targetIP )
{
   //printf( "Send ARP Request for %3d.%3d.%3d.%3d\n",
   //      targetIP[ 0 ],
   //      targetIP[ 1 ],
   //      targetIP[ 2 ],
   //      targetIP[ 3 ] );

   ARPRequest.Initialize();
   ARPRequest.Packet    += ProtocolMACEthernet::HeaderSize();
   ARPRequest.Remainder -= ProtocolMACEthernet::HeaderSize();

   ARPRequest.Length += SenderHardwareAddressOffset + Config.Address.HardwareSize*2 + Config.Address.ProtocolSize*2;
   ARPRequest.Disposable = false;

   ARPRequest.Packet[  0 ] = 0x00;  // Hardware Type
   ARPRequest.Packet[  1 ] = 0x01;
   ARPRequest.Packet[  2 ] = 0x08;  // Protocol Type
   ARPRequest.Packet[  3 ] = 0x00;
   ARPRequest.Packet[  4 ] = 0x06;  // Hardware Size
   ARPRequest.Packet[  5 ] = 0x04;  // Protocol Size
   ARPRequest.Packet[  6 ] = 0x00;  // Op
   ARPRequest.Packet[  7 ] = 0x01;

   ARPRequest.Packet[  8 ] = Config.Address.Hardware[ 0 ];
   ARPRequest.Packet[  9 ] = Config.Address.Hardware[ 1 ];
   ARPRequest.Packet[ 10 ] = Config.Address.Hardware[ 2 ];
   ARPRequest.Packet[ 11 ] = Config.Address.Hardware[ 3 ];
   ARPRequest.Packet[ 12 ] = Config.Address.Hardware[ 4 ];
   ARPRequest.Packet[ 13 ] = Config.Address.Hardware[ 5 ];

   ARPRequest.Packet[ 14 ] = Config.Address.Protocol[ 0 ];
   ARPRequest.Packet[ 15 ] = Config.Address.Protocol[ 1 ];
   ARPRequest.Packet[ 16 ] = Config.Address.Protocol[ 2 ];
   ARPRequest.Packet[ 17 ] = Config.Address.Protocol[ 3 ];

   ARPRequest.Packet[ 18 ] = 0;
   ARPRequest.Packet[ 19 ] = 0;
   ARPRequest.Packet[ 20 ] = 0;
   ARPRequest.Packet[ 21 ] = 0;
   ARPRequest.Packet[ 22 ] = 0;
   ARPRequest.Packet[ 23 ] = 0;

   ARPRequest.Packet[ 24 ] = targetIP[ 0 ];
   ARPRequest.Packet[ 25 ] = targetIP[ 1 ];
   ARPRequest.Packet[ 26 ] = targetIP[ 2 ];
   ARPRequest.Packet[ 27 ] = targetIP[ 3 ];

   ProtocolMACEthernet::Transmit( &ARPRequest, Config.BroadcastMACAddress, 0x0806 );
}

//============================================================================
//
//============================================================================

uint8_t* ProtocolARP::Protocol2Hardware( uint8_t* protocolAddress )
{
   int index;
   
   if( !IsLocal(protocolAddress) )
   {
      protocolAddress = Config.Gateway;
   }
   index = LocateProtocolAddress( protocolAddress );

   if( index != -1 )
   {
      return Cache[ index ].Hardware;
   }
   else
   {
      SendRequest( protocolAddress );
      return 0;
   }
}

//============================================================================
//
//============================================================================

bool ProtocolARP::IsLocal( uint8_t* protocolAddress )
{
   int i;

   for( i=0; i<Address::ProtocolSize; i++ )
   {
      if
      (
         (protocolAddress[ i ] & Config.SubnetMask[ i ]) !=
         (Config.Address.Protocol[ i ] & Config.SubnetMask[ i ])
      )
      {
         break;
      }
   }

   return i == Address::ProtocolSize;
}

//============================================================================
//
//============================================================================

int ProtocolARP::LocateProtocolAddress( uint8_t* protocolAddress )
{
   int i;
   int j;

   //printf( "Check ARP cache for %3d.%3d.%3d.%3d\n",
   //   protocolAddress[ 0 ],
   //   protocolAddress[ 1 ],
   //   protocolAddress[ 2 ],
   //   protocolAddress[ 3 ] );

   for( i=0; i<CacheSize; i++ )
   {
      // Go through the address backwards since least significant byte is most
      // likely to be unique
      for( j=Address::ProtocolSize-1; j>=0; j-- )
      {
         if( Cache[ i ].Protocol[ j ] != protocolAddress[ j ] )
         {
            break;
         }
      }
      if( j == -1 )
      {
         // found
         //printf( "Found MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
         //   Cache[ i ].Hardware[ 0 ],
         //   Cache[ i ].Hardware[ 1 ],
         //   Cache[ i ].Hardware[ 2 ],
         //   Cache[ i ].Hardware[ 3 ],
         //   Cache[ i ].Hardware[ 4 ],
         //   Cache[ i ].Hardware[ 5 ] );
         return i;
      }
   }

   return -1;
}
