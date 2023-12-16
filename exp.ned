//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

simple tsn_switch
{
    parameters:
        bool sendMsgOnInit = default(false);
        int streamNum = default(100);
        bool isTalker = default(false);
       	int sequenceNum = default(10);
       	int k = default(6);			// 发送数据块数目
       	int m = default(1);			// 发送编码块数目
     	int sendMode = default(0); 	// 0-编码模式 1-副本模式
    gates:
        inout gate[];
}

network exp
{
    types:
        channel primaryChannel extends ned.DatarateChannel {
        	delay = 1000ms;
        	ber = 5e-10;
        	datarate = 100Mbps;
        }
        channel secondaryChannel extends ned.DatarateChannel {
        	delay = 1000ms;
        	ber = 5e-10;
        	datarate = 100Mbps;
        }
    submodules:
        talker: tsn_switch {
            parameters:
                sendMsgOnInit = true;
                isTalker = true;
                sendMode = 0;
            @display("i=device/switch");
        }
        listener: tsn_switch {
            parameters:
                sendMode = 0;
            @display("i=device/switch");
        }
    connections:
        talker.gate++ <--> primaryChannel  <--> listener.gate++;
        talker.gate++ <--> secondaryChannel <--> listener.gate++;
}
