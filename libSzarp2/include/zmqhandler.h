#ifndef ZMQHANDLER_H
#define ZMQHANDLER_H
/* 
  SZARP: SCADA software 
  

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
*/

#include <unordered_map>
#include <zmq.hpp>

#include "protobuf/paramsvalues.pb.h"

#include "szarp_config.h"

class zmqhandler {
	zmq::socket_t m_sub_sock;
	zmq::socket_t m_pub_sock;

	size_t m_pubs_idx;
	szarp::ParamsValues m_pubs;

	std::vector<szarp::ParamValue> m_send;
	std::unordered_map<size_t, size_t> m_send_map;

	void process_msg(szarp::ParamsValues& values);

public:
	template <typename Config, typename Device>
	zmqhandler(
		const Config& config,
		const Device& device,
		zmq::context_t& context,
		const std::string& sub_address,
		const std::string& pub_address)
		:
		m_sub_sock(context, ZMQ_SUB),
		m_pub_sock(context, ZMQ_PUB) {

		m_pubs_idx = config.GetFirstParamIpcInd(device);

		// Ignore units for sends
		auto param_sent_no = 0;
		for (const auto send_ipc_ind: config.GetSendIpcInds(device)) {
			m_send_map[send_ipc_ind] = param_sent_no++;
		}

		m_send.resize(param_sent_no);

		m_pub_sock.connect(pub_address.c_str());
		if (m_send.size())
			m_sub_sock.connect(sub_address.c_str());

		int zero = 0;
		m_pub_sock.setsockopt(ZMQ_LINGER, &zero, sizeof(zero));
		m_sub_sock.setsockopt(ZMQ_LINGER, &zero, sizeof(zero));

		m_sub_sock.setsockopt(ZMQ_SUBSCRIBE, "", 0);
	}

	template <typename Config, typename Device, typename... Ts>
	zmqhandler(const Config* config, const Device* device, Ts&&... args): zmqhandler(*config, *device, std::forward<Ts>(args)...) {}

	template<class T, class V> void set_value(size_t i, const T& t, const V& value);
	szarp::ParamValue& get_value(size_t i);

	int subsocket();

	void publish();
	void receive();
};

#endif
