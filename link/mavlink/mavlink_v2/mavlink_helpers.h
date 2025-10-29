#pragma once

#include "string.h"
#include "checksum.h"
#include "mavlink_types.h"
#include "mavlink_conversions.h"
#include <stdio.h>

#ifndef MAVLINK_HELPER
#define MAVLINK_HELPER
#endif

#include "mavlink_sha256.h"

#ifdef MAVLINK_USE_CXX_NAMESPACE
namespace mavlink {
#endif

/**
 * @brief 内部函数，用于获取每个通道的状态访问权限
 * 
 * 该函数为每个MAVLink通信通道提供状态结构体的访问接口。
 * 支持外部定义状态数组或使用内部静态数组。
 * 
 * @param[in] chan 通道ID，范围为0到MAVLINK_COMM_NUM_BUFFERS-1
 * @return 指向指定通道状态结构体的指针
 */
#ifndef MAVLINK_GET_CHANNEL_STATUS
MAVLINK_HELPER mavlink_status_t* mavlink_get_channel_status(uint8_t chan)
{
#ifdef MAVLINK_EXTERNAL_RX_STATUS
	// 函数内未定义m_mavlink_status数组，
	// 必须在外部定义
#else
	static mavlink_status_t m_mavlink_status[MAVLINK_COMM_NUM_BUFFERS];
#endif
	return &m_mavlink_status[chan];
}
#endif

/**
 * @brief 内部函数，用于获取每个通道的消息缓冲区访问权限
 * 
 * 该函数为每个MAVLink通信通道提供消息缓冲区的访问接口。
 * 支持外部定义缓冲区数组或使用内部静态数组。
 * 
 * @param[in] chan 通道ID，范围为0到MAVLINK_COMM_NUM_BUFFERS-1
 * @return 指向指定通道消息缓冲区的指针
 */
#ifndef MAVLINK_GET_CHANNEL_BUFFER
MAVLINK_HELPER mavlink_message_t* mavlink_get_channel_buffer(uint8_t chan)
{
	
#ifdef MAVLINK_EXTERNAL_RX_BUFFER
	// 函数内未定义m_mavlink_buffer数组，
	// 必须在外部定义
#else
	static mavlink_message_t m_mavlink_buffer[MAVLINK_COMM_NUM_BUFFERS];
#endif
	return &m_mavlink_buffer[chan];
}
#endif // MAVLINK_GET_CHANNEL_BUFFER

/* 启用此选项以检查每条消息的长度。
    这允许更早地捕获无效消息。如果传输介质容易丢失（或增加）字符
    （例如信号时强时弱的无线电），请使用此选项。
    仅在通道只包含头文件中列出的消息类型时使用。
*/
//#define MAVLINK_CHECK_MESSAGE_LENGTH

/**
 * @brief 重置通道的状态
 * 
 * 将指定通道的解析状态重置为空闲状态，用于错误恢复或初始化。
 * 
 * @param[in] chan 要重置的通道ID
 */
MAVLINK_HELPER void mavlink_reset_channel_status(uint8_t chan)
{
	mavlink_status_t *status = mavlink_get_channel_status(chan);
	status->parse_state = MAVLINK_PARSE_STATE_IDLE;
}

#ifndef MAVLINK_NO_SIGN_PACKET
/**
 * @brief 为数据包创建签名块
 * 
 * 使用SHA256算法为MAVLink数据包生成数字签名，用于验证消息的完整性和来源。
 * 签名包含链路ID、时间戳和48位SHA256哈希值。
 * 
 * @param[in] signing 签名配置结构体指针
 * @param[out] signature 输出的签名块缓冲区，长度为MAVLINK_SIGNATURE_BLOCK_LEN
 * @param[in] header 消息头部数据
 * @param[in] header_len 头部数据长度
 * @param[in] packet 消息载荷数据
 * @param[in] packet_len 载荷数据长度
 * @param[in] crc CRC校验值（2字节）
 * @return 签名块长度，如果不需要签名则返回0
 */
MAVLINK_HELPER uint8_t mavlink_sign_packet(mavlink_signing_t *signing,
					   uint8_t signature[MAVLINK_SIGNATURE_BLOCK_LEN],
					   const uint8_t *header, uint8_t header_len,
					   const uint8_t *packet, uint8_t packet_len,
					   const uint8_t crc[2])
{
	mavlink_sha256_ctx ctx;
	union {
	    uint64_t t64;
	    uint8_t t8[8];
	} tstamp;
	if (signing == NULL || !(signing->flags & MAVLINK_SIGNING_FLAG_SIGN_OUTGOING)) {
	    return 0;
	}
	signature[0] = signing->link_id;
	tstamp.t64 = signing->timestamp;
	memcpy(&signature[1], tstamp.t8, 6);
	signing->timestamp++;
	
	mavlink_sha256_init(&ctx);
	mavlink_sha256_update(&ctx, signing->secret_key, sizeof(signing->secret_key));
	mavlink_sha256_update(&ctx, header, header_len);
	mavlink_sha256_update(&ctx, packet, packet_len);
	mavlink_sha256_update(&ctx, crc, 2);
	mavlink_sha256_update(&ctx, signature, 7);
	mavlink_sha256_final_48(&ctx, &signature[7]);
	
	return MAVLINK_SIGNATURE_BLOCK_LEN;
}
#endif

/**
 * @brief 修剪载荷中尾部的零填充字节（仅限MAVLink 2）
 *
 * 该函数移除载荷末尾的零字节，以减少传输的数据量。
 * 这是MAVLink 2协议的优化特性。
 *
 * @param[in] payload 序列化的载荷缓冲区
 * @param[in] length 完整载荷缓冲区的长度
 * @return 修剪零填充字节后的载荷长度
 */
MAVLINK_HELPER uint8_t _mav_trim_payload(const char *payload, uint8_t length)
{
	while (length > 1 && payload[length-1] == 0) {
		length--;
	}
	return length;
}

#ifndef MAVLINK_NO_SIGNATURE_CHECK
/**
 * @brief 检查数据包的签名块
 * 
 * 验证接收到的MAVLink消息的数字签名是否有效。检查包括：
 * 1. SHA256签名验证
 * 2. 时间戳检查（防止重放攻击）
 * 3. 签名流管理
 * 
 * @param[in] signing 签名配置结构体指针
 * @param[in,out] signing_streams 签名流管理结构体指针
 * @param[in] msg 要验证的消息
 * @return true表示签名有效，false表示签名无效
 */
MAVLINK_HELPER bool mavlink_signature_check(mavlink_signing_t *signing,
					    mavlink_signing_streams_t *signing_streams,
					    const mavlink_message_t *msg)
{
	if (signing == NULL) {
		return true;
	}
        const uint8_t *p = (const uint8_t *)&msg->magic;
	const uint8_t *psig = msg->signature;
        const uint8_t *incoming_signature = psig+7;
	mavlink_sha256_ctx ctx;
	uint8_t signature[6];
	uint16_t i;
        
	mavlink_sha256_init(&ctx);
	mavlink_sha256_update(&ctx, signing->secret_key, sizeof(signing->secret_key));
	mavlink_sha256_update(&ctx, p, MAVLINK_NUM_HEADER_BYTES);
	mavlink_sha256_update(&ctx, _MAV_PAYLOAD(msg), msg->len);
	mavlink_sha256_update(&ctx, msg->ck, 2);
	mavlink_sha256_update(&ctx, psig, 1+6);
	mavlink_sha256_final_48(&ctx, signature);
        if (memcmp(signature, incoming_signature, 6) != 0) {
                signing->last_status = MAVLINK_SIGNING_STATUS_BAD_SIGNATURE;
		return false;
	}

	// now check timestamp
	union tstamp {
	    uint64_t t64;
	    uint8_t t8[8];
	} tstamp;
	uint8_t link_id = psig[0];
	tstamp.t64 = 0;
	memcpy(tstamp.t8, psig+1, 6);

	if (signing_streams == NULL) {
                signing->last_status = MAVLINK_SIGNING_STATUS_NO_STREAMS;
                return false;
	}
	
	// find stream
	for (i=0; i<signing_streams->num_signing_streams; i++) {
		if (msg->sysid == signing_streams->stream[i].sysid &&
		    msg->compid == signing_streams->stream[i].compid &&
		    link_id == signing_streams->stream[i].link_id) {
			break;
		}
	}
	if (i == signing_streams->num_signing_streams) {
		if (signing_streams->num_signing_streams >= MAVLINK_MAX_SIGNING_STREAMS) {
			// over max number of streams
                        signing->last_status = MAVLINK_SIGNING_STATUS_TOO_MANY_STREAMS;
                        return false;
		}
		// new stream. Only accept if timestamp is not more than 1 minute old
		if (tstamp.t64 + 6000*1000UL < signing->timestamp) {
                        signing->last_status = MAVLINK_SIGNING_STATUS_OLD_TIMESTAMP;
                        return false;
		}
		// add new stream
		signing_streams->stream[i].sysid = msg->sysid;
		signing_streams->stream[i].compid = msg->compid;
		signing_streams->stream[i].link_id = link_id;
		signing_streams->num_signing_streams++;
	} else {
		union tstamp last_tstamp;
		last_tstamp.t64 = 0;
		memcpy(last_tstamp.t8, signing_streams->stream[i].timestamp_bytes, 6);
		if (tstamp.t64 <= last_tstamp.t64) {
			// repeating old timestamp
                        signing->last_status = MAVLINK_SIGNING_STATUS_REPLAY;
                        return false;
		}
	}

	// remember last timestamp
	memcpy(signing_streams->stream[i].timestamp_bytes, psig+1, 6);

	// our next timestamp must be at least this timestamp
	if (tstamp.t64 > signing->timestamp) {
		signing->timestamp = tstamp.t64;
	}
        signing->last_status = MAVLINK_SIGNING_STATUS_OK;
        return true;
}
#endif


/**
 * @brief Finalize a MAVLink message with channel assignment
 *
 * This function calculates the checksum and sets length and aircraft id correctly.
 * It assumes that the message id and the payload are already correctly set. This function
 * can also be used if the message header has already been written before (as in mavlink_msg_xxx_pack
 * instead of mavlink_msg_xxx_pack_headerless), it just introduces little extra overhead.
 *
 * @param msg Message to finalize
 * @param system_id Id of the sending (this) system, 1-127
 * @param length Message length
 */
/**
 * @brief 完成MAVLink消息的最终处理（缓冲区版本）
 * 
 * 该函数计算校验和并正确设置消息长度和系统ID。
 * 假设消息ID和载荷已正确设置。
 * 
 * @param[in,out] msg 要处理的消息结构体
 * @param[in] system_id 发送系统的ID（1-127）
 * @param[in] component_id 发送组件的ID
 * @param[in] status 通道状态结构体
 * @param[in] min_length 消息的最小长度
 * @param[in] length 消息的实际长度
 * @param[in] crc_extra 额外的CRC字节
 * @return 最终消息的总长度（包括头部、载荷和校验和）
 */
MAVLINK_HELPER uint16_t mavlink_finalize_message_buffer(mavlink_message_t* msg, uint8_t system_id, uint8_t component_id,
						      mavlink_status_t* status, uint8_t min_length, uint8_t length, uint8_t crc_extra)
{
	bool mavlink1 = (status->flags & MAVLINK_STATUS_FLAG_OUT_MAVLINK1) != 0;
#ifndef MAVLINK_NO_SIGN_PACKET
	bool signing = 	(!mavlink1) && status->signing && (status->signing->flags & MAVLINK_SIGNING_FLAG_SIGN_OUTGOING);
#else
	bool signing = false;
#endif
	uint8_t signature_len = signing? MAVLINK_SIGNATURE_BLOCK_LEN : 0;
        uint8_t header_len = MAVLINK_CORE_HEADER_LEN+1;
	uint8_t buf[MAVLINK_CORE_HEADER_LEN+1];
	if (mavlink1) {
		msg->magic = MAVLINK_STX_MAVLINK1;
		header_len = MAVLINK_CORE_HEADER_MAVLINK1_LEN+1;
	} else {
		msg->magic = MAVLINK_STX;
	}
	msg->len = mavlink1?min_length:_mav_trim_payload(_MAV_PAYLOAD(msg), length);
	msg->sysid = system_id;
	msg->compid = component_id;
	msg->incompat_flags = 0;
	if (signing) {
		msg->incompat_flags |= MAVLINK_IFLAG_SIGNED;
	}
	msg->compat_flags = 0;
	msg->seq = status->current_tx_seq;
	status->current_tx_seq = status->current_tx_seq + 1;

	// form the header as a byte array for the crc
	buf[0] = msg->magic;
	buf[1] = msg->len;
	if (mavlink1) {
		buf[2] = msg->seq;
		buf[3] = msg->sysid;
		buf[4] = msg->compid;
		buf[5] = msg->msgid & 0xFF;
	} else {
		buf[2] = msg->incompat_flags;
		buf[3] = msg->compat_flags;
		buf[4] = msg->seq;
		buf[5] = msg->sysid;
		buf[6] = msg->compid;
		buf[7] = msg->msgid & 0xFF;
		buf[8] = (msg->msgid >> 8) & 0xFF;
		buf[9] = (msg->msgid >> 16) & 0xFF;
	}
	
	uint16_t checksum = crc_calculate(&buf[1], header_len-1);
	crc_accumulate_buffer(&checksum, _MAV_PAYLOAD(msg), msg->len);
	crc_accumulate(crc_extra, &checksum);
	mavlink_ck_a(msg) = (uint8_t)(checksum & 0xFF);
	mavlink_ck_b(msg) = (uint8_t)(checksum >> 8);

	msg->checksum = checksum;

#ifndef MAVLINK_NO_SIGN_PACKET
	if (signing) {
		mavlink_sign_packet(status->signing,
				    msg->signature,
				    (const uint8_t *)buf, header_len,
				    (const uint8_t *)_MAV_PAYLOAD(msg), msg->len,
				    (const uint8_t *)_MAV_PAYLOAD(msg)+(uint16_t)msg->len);
	}
#endif

	return msg->len + header_len + 2 + signature_len;
}

/**
 * @brief 完成MAVLink消息的最终处理（通道版本）
 * 
 * 该函数是mavlink_finalize_message_buffer的通道包装版本，
 * 自动获取指定通道的状态信息。
 * 
 * @param[in,out] msg 要处理的消息结构体
 * @param[in] system_id 发送系统的ID（1-127）
 * @param[in] component_id 发送组件的ID
 * @param[in] chan 通道ID
 * @param[in] min_length 消息的最小长度
 * @param[in] length 消息的实际长度
 * @param[in] crc_extra 额外的CRC字节
 * @return 最终消息的总长度（包括头部、载荷和校验和）
 */
MAVLINK_HELPER uint16_t mavlink_finalize_message_chan(mavlink_message_t* msg, uint8_t system_id, uint8_t component_id,
						      uint8_t chan, uint8_t min_length, uint8_t length, uint8_t crc_extra)
{
	mavlink_status_t *status = mavlink_get_channel_status(chan);
	return mavlink_finalize_message_buffer(msg, system_id, component_id, status, min_length, length, crc_extra);
}

/**
 * @brief 完成MAVLink消息的最终处理（默认通道版本）
 * 
 * 该函数是mavlink_finalize_message_chan的简化版本，
 * 使用MAVLINK_COMM_0作为默认通道。
 * 
 * @param[in,out] msg 要处理的消息结构体
 * @param[in] system_id 发送系统的ID（1-127）
 * @param[in] component_id 发送组件的ID
 * @param[in] min_length 消息的最小长度
 * @param[in] length 消息的实际长度
 * @param[in] crc_extra 额外的CRC字节
 * @return 最终消息的总长度（包括头部、载荷和校验和）
 */
MAVLINK_HELPER uint16_t mavlink_finalize_message(mavlink_message_t* msg, uint8_t system_id, uint8_t component_id, 
						 uint8_t min_length, uint8_t length, uint8_t crc_extra)
{
    return mavlink_finalize_message_chan(msg, system_id, component_id, MAVLINK_COMM_0, min_length, length, crc_extra);
}

/**
 * @brief 记录解析错误
 * 
 * 内部函数，用于在MAVLink消息解析过程中记录错误。
 * 增加状态结构体中的parse_error计数器。
 * 
 * @param[in,out] status 通道状态结构体
 */
static inline void _mav_parse_error(mavlink_status_t *status)
{
    status->parse_error++;
}

#ifdef MAVLINK_USE_CONVENIENCE_FUNCTIONS
/**
 * @brief 通过UART发送数据
 * 
 * 内部函数，用于通过指定通道发送数据。
 * 根据平台定义选择逐字节发送或整包发送。
 * 
 * @param[in] chan 通道ID
 * @param[in] buf 要发送的数据缓冲区
 * @param[in] len 数据长度
 */
MAVLINK_HELPER void _mavlink_send_uart(mavlink_channel_t chan, const char *buf, uint16_t len);

/**
 * @brief Finalize a MAVLink message with channel assignment and send
 */
MAVLINK_HELPER void _mav_finalize_message_chan_send(mavlink_channel_t chan, uint32_t msgid,
                                                    const char *packet, 
						    uint8_t min_length, uint8_t length, uint8_t crc_extra)
{
	uint16_t checksum;
	uint8_t buf[MAVLINK_NUM_HEADER_BYTES];
	uint8_t ck[2];
	mavlink_status_t *status = mavlink_get_channel_status(chan);
        uint8_t header_len = MAVLINK_CORE_HEADER_LEN;
	uint8_t signature_len = 0;
	uint8_t signature[MAVLINK_SIGNATURE_BLOCK_LEN];
	bool mavlink1 = (status->flags & MAVLINK_STATUS_FLAG_OUT_MAVLINK1) != 0;
	bool signing = 	(!mavlink1) && status->signing && (status->signing->flags & MAVLINK_SIGNING_FLAG_SIGN_OUTGOING);

        if (mavlink1) {
            length = min_length;
            if (msgid > 255) {
                // can't send 16 bit messages
                _mav_parse_error(status);
                return;
            }
            header_len = MAVLINK_CORE_HEADER_MAVLINK1_LEN;
            buf[0] = MAVLINK_STX_MAVLINK1;
            buf[1] = length;
            buf[2] = status->current_tx_seq;
            buf[3] = mavlink_system.sysid;
            buf[4] = mavlink_system.compid;
            buf[5] = msgid & 0xFF;
        } else {
	    uint8_t incompat_flags = 0;
	    if (signing) {
		incompat_flags |= MAVLINK_IFLAG_SIGNED;
	    }
            length = _mav_trim_payload(packet, length);
            buf[0] = MAVLINK_STX;
            buf[1] = length;
            buf[2] = incompat_flags;
            buf[3] = 0; // compat_flags
            buf[4] = status->current_tx_seq;
            buf[5] = mavlink_system.sysid;
            buf[6] = mavlink_system.compid;
            buf[7] = msgid & 0xFF;
            buf[8] = (msgid >> 8) & 0xFF;
            buf[9] = (msgid >> 16) & 0xFF;
        }
	status->current_tx_seq++;
	checksum = crc_calculate((const uint8_t*)&buf[1], header_len);
	crc_accumulate_buffer(&checksum, packet, length);
	crc_accumulate(crc_extra, &checksum);
	ck[0] = (uint8_t)(checksum & 0xFF);
	ck[1] = (uint8_t)(checksum >> 8);

#ifndef MAVLINK_NO_SIGN_PACKET
	if (signing) {
		// possibly add a signature
		signature_len = mavlink_sign_packet(status->signing, signature, buf, header_len+1,
						    (const uint8_t *)packet, length, ck);
	}
#endif

	MAVLINK_START_UART_SEND(chan, header_len + 3 + (uint16_t)length + (uint16_t)signature_len);
	_mavlink_send_uart(chan, (const char *)buf, header_len+1);
	_mavlink_send_uart(chan, packet, length);
	_mavlink_send_uart(chan, (const char *)ck, 2);
	if (signature_len != 0) {
		_mavlink_send_uart(chan, (const char *)signature, signature_len);
	}
	MAVLINK_END_UART_SEND(chan, header_len + 3 + (uint16_t)length + (uint16_t)signature_len);
}

/**
 * @brief re-send a message over a uart channel
 * this is more stack efficient than re-marshalling the message
 * If the message is signed then the original signature is also sent
 */
MAVLINK_HELPER void _mavlink_resend_uart(mavlink_channel_t chan, const mavlink_message_t *msg)
{
	uint8_t ck[2];

	ck[0] = (uint8_t)(msg->checksum & 0xFF);
	ck[1] = (uint8_t)(msg->checksum >> 8);
	// XXX use the right sequence here

        uint8_t header_len;
        uint8_t signature_len;
        
        if (msg->magic == MAVLINK_STX_MAVLINK1) {
            header_len = MAVLINK_CORE_HEADER_MAVLINK1_LEN + 1;
            signature_len = 0;
            MAVLINK_START_UART_SEND(chan, header_len + msg->len + 2 + signature_len);
            // we can't send the structure directly as it has extra mavlink2 elements in it
            uint8_t buf[MAVLINK_CORE_HEADER_MAVLINK1_LEN + 1];
            buf[0] = msg->magic;
            buf[1] = msg->len;
            buf[2] = msg->seq;
            buf[3] = msg->sysid;
            buf[4] = msg->compid;
            buf[5] = msg->msgid & 0xFF;
            _mavlink_send_uart(chan, (const char*)buf, header_len);
        } else {
            header_len = MAVLINK_CORE_HEADER_LEN + 1;
            signature_len = (msg->incompat_flags & MAVLINK_IFLAG_SIGNED)?MAVLINK_SIGNATURE_BLOCK_LEN:0;
            MAVLINK_START_UART_SEND(chan, header_len + msg->len + 2 + signature_len);
            uint8_t buf[MAVLINK_CORE_HEADER_LEN + 1];
            buf[0] = msg->magic;
            buf[1] = msg->len;
            buf[2] = msg->incompat_flags;
            buf[3] = msg->compat_flags;
            buf[4] = msg->seq;
            buf[5] = msg->sysid;
            buf[6] = msg->compid;
            buf[7] = msg->msgid & 0xFF;
            buf[8] = (msg->msgid >> 8) & 0xFF;
            buf[9] = (msg->msgid >> 16) & 0xFF;
            _mavlink_send_uart(chan, (const char *)buf, header_len);
        }
	_mavlink_send_uart(chan, _MAV_PAYLOAD(msg), msg->len);
	_mavlink_send_uart(chan, (const char *)ck, 2);
        if (signature_len != 0) {
	    _mavlink_send_uart(chan, (const char *)msg->signature, MAVLINK_SIGNATURE_BLOCK_LEN);
        }
        MAVLINK_END_UART_SEND(chan, header_len + msg->len + 2 + signature_len);
}
#endif // MAVLINK_USE_CONVENIENCE_FUNCTIONS

/**
 * @brief Pack a message to send it over a serial byte stream
 */
MAVLINK_HELPER uint16_t mavlink_msg_to_send_buffer(uint8_t *buf, const mavlink_message_t *msg)
{
	uint8_t signature_len, header_len;
	uint8_t *ck;
        uint8_t length = msg->len;
        
	if (msg->magic == MAVLINK_STX_MAVLINK1) {
		signature_len = 0;
		header_len = MAVLINK_CORE_HEADER_MAVLINK1_LEN;
		buf[0] = msg->magic;
		buf[1] = length;
		buf[2] = msg->seq;
		buf[3] = msg->sysid;
		buf[4] = msg->compid;
		buf[5] = msg->msgid & 0xFF;
		memcpy(&buf[6], _MAV_PAYLOAD(msg), msg->len);
		ck = buf + header_len + 1 + (uint16_t)msg->len;
	} else {
		length = _mav_trim_payload(_MAV_PAYLOAD(msg), length);
		header_len = MAVLINK_CORE_HEADER_LEN;
		buf[0] = msg->magic;
		buf[1] = length;
		buf[2] = msg->incompat_flags;
		buf[3] = msg->compat_flags;
		buf[4] = msg->seq;
		buf[5] = msg->sysid;
		buf[6] = msg->compid;
		buf[7] = msg->msgid & 0xFF;
		buf[8] = (msg->msgid >> 8) & 0xFF;
		buf[9] = (msg->msgid >> 16) & 0xFF;
		memcpy(&buf[10], _MAV_PAYLOAD(msg), length);
		ck = buf + header_len + 1 + (uint16_t)length;
		signature_len = (msg->incompat_flags & MAVLINK_IFLAG_SIGNED)?MAVLINK_SIGNATURE_BLOCK_LEN:0;
	}
	ck[0] = (uint8_t)(msg->checksum & 0xFF);
	ck[1] = (uint8_t)(msg->checksum >> 8);
	if (signature_len > 0) {
		memcpy(&ck[2], msg->signature, signature_len);
	}

	return header_len + 1 + 2 + (uint16_t)length + (uint16_t)signature_len;
}

union __mavlink_bitfield {
	uint8_t uint8;
	int8_t int8;
	uint16_t uint16;
	int16_t int16;
	uint32_t uint32;
	int32_t int32;
};


/**
 * @brief 初始化消息的校验和计算
 * 
 * 在开始解析新消息时调用，初始化校验和计算。
 * 
 * @param[in,out] msg 消息结构体
 */
MAVLINK_HELPER void mavlink_start_checksum(mavlink_message_t* msg)
{
	uint16_t crcTmp = 0;
	crc_init(&crcTmp);
	msg->checksum = crcTmp;
}

/**
 * @brief 更新消息的校验和
 * 
 * 在解析消息的每个字节时调用，更新校验和计算。
 * 
 * @param[in,out] msg 消息结构体
 * @param[in] c 当前处理的字节
 */
MAVLINK_HELPER void mavlink_update_checksum(mavlink_message_t* msg, uint8_t c)
{
	uint16_t checksum = msg->checksum;
	crc_accumulate(c, &checksum);
	msg->checksum = checksum;
}

/**
 * @brief 根据消息ID获取CRC条目
 * 
 * 使用二分查找在CRC表中查找指定消息ID的条目。
 * 该表必须按消息ID排序。
 * 
 * @param[in] msgid 消息ID
 * @return 指向CRC条目的指针，如果未找到则返回NULL
 */
#ifndef MAVLINK_GET_MSG_ENTRY
MAVLINK_HELPER const mavlink_msg_entry_t *mavlink_get_msg_entry(uint32_t msgid)
{
	static const mavlink_msg_entry_t mavlink_message_crcs[] = MAVLINK_MESSAGE_CRCS;
        /*
	  使用二分查找来找到正确的条目。完美哈希可能更好
	  注意：这假设表按消息ID排序
	*/
        uint32_t low=0, high=sizeof(mavlink_message_crcs)/sizeof(mavlink_message_crcs[0]) - 1;
        while (low < high) {
            uint32_t mid = (low+1+high)/2;
            if (msgid < mavlink_message_crcs[mid].msgid) {
                high = mid-1;
                continue;
            }
            if (msgid > mavlink_message_crcs[mid].msgid) {
                low = mid;
                continue;
            }
            low = mid;
            break;
        }
        if (mavlink_message_crcs[low].msgid != msgid) {
            // msgid is not in the table
            return NULL;
        }
        return &mavlink_message_crcs[low];
}
#endif // MAVLINK_GET_MSG_ENTRY

/**
 * @brief 获取消息的额外CRC值
 * 
 * 根据消息ID从CRC表中查找对应的额外CRC值。
 * 
 * @param[in] msg 消息结构体
 * @return 额外CRC值，如果未找到则返回0
 */
MAVLINK_HELPER uint8_t mavlink_get_crc_extra(const mavlink_message_t *msg)
{
	const mavlink_msg_entry_t *e = mavlink_get_msg_entry(msg->msgid);
	return e?e->crc_extra:0;
}

/**
 * @brief 获取消息的最小长度
 * 
 * 根据消息ID从CRC表中查找对应的最小消息长度。
 * 
 * @param[in] msg 消息结构体
 * @return 最小消息长度，如果未找到则返回0
 */
#define MAVLINK_HAVE_MIN_MESSAGE_LENGTH
MAVLINK_HELPER uint8_t mavlink_min_message_length(const mavlink_message_t *msg)
{
	const mavlink_msg_entry_t *e = mavlink_get_msg_entry(msg->msgid);
        return e?e->min_msg_len:0;
}

/**
 * @brief 获取消息的最大长度（包括扩展部分）
 * 
 * 根据消息ID从CRC表中查找对应的最大消息长度。
 * 
 * @param[in] msg 消息结构体
 * @return 最大消息长度，如果未找到则返回0
 */
#define MAVLINK_HAVE_MAX_MESSAGE_LENGTH
MAVLINK_HELPER uint8_t mavlink_max_message_length(const mavlink_message_t *msg)
{
	const mavlink_msg_entry_t *e = mavlink_get_msg_entry(msg->msgid);
        return e?e->max_msg_len:0;
}

/**
 * @brief 带调用者提供缓冲区的MAVLink字符解析函数
 * 
 * 这是mavlink_frame_char()的变体，允许调用者提供解析缓冲区。
 * 适用于不希望使用全局变量的库中实现MAVLink解析器。
 * 
 * @param[in,out] rxmsg 解析消息缓冲区
 * @param[in,out] status 解析状态缓冲区
 * @param[in] c 要解析的字符
 * @param[out] r_message 如果解码成功，存储解码后的消息数据；否则为NULL
 * @param[out] r_mavlink_status 如果解码成功，填充通道的统计信息
 * @return 0表示未解码出消息，1表示成功解码且CRC正确，2表示CRC错误
 */
MAVLINK_HELPER uint8_t mavlink_frame_char_buffer(mavlink_message_t* rxmsg, 
                                                 mavlink_status_t* status,
                                                 uint8_t c, 
                                                 mavlink_message_t* r_message, 
                                                 mavlink_status_t* r_mavlink_status)
{

	status->msg_received = MAVLINK_FRAMING_INCOMPLETE;

	switch (status->parse_state)
	{
	case MAVLINK_PARSE_STATE_UNINIT:
	case MAVLINK_PARSE_STATE_IDLE:
		if (c == MAVLINK_STX)
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_STX;
			rxmsg->len = 0;
			rxmsg->magic = c;
                        status->flags &= ~MAVLINK_STATUS_FLAG_IN_MAVLINK1;
			mavlink_start_checksum(rxmsg);
		} else if (c == MAVLINK_STX_MAVLINK1)
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_STX;
			rxmsg->len = 0;
			rxmsg->magic = c;
                        status->flags |= MAVLINK_STATUS_FLAG_IN_MAVLINK1;
			mavlink_start_checksum(rxmsg);
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_STX:
			if (status->msg_received 
/* Support shorter buffers than the
   default maximum packet size */
#if (MAVLINK_MAX_PAYLOAD_LEN < 255)
				|| c > MAVLINK_MAX_PAYLOAD_LEN
#endif
				)
		{
			status->buffer_overrun++;
			_mav_parse_error(status);
			status->msg_received = MAVLINK_FRAMING_INCOMPLETE;
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
		}
		else
		{
			// NOT counting STX, LENGTH, SEQ, SYSID, COMPID, MSGID, CRC1 and CRC2
			rxmsg->len = c;
			status->packet_idx = 0;
			mavlink_update_checksum(rxmsg, c);
                        if (status->flags & MAVLINK_STATUS_FLAG_IN_MAVLINK1) {
                            rxmsg->incompat_flags = 0;
                            rxmsg->compat_flags = 0;
                            status->parse_state = MAVLINK_PARSE_STATE_GOT_COMPAT_FLAGS;
                        } else {
                            status->parse_state = MAVLINK_PARSE_STATE_GOT_LENGTH;
                        }
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_LENGTH:
		rxmsg->incompat_flags = c;
		if ((rxmsg->incompat_flags & ~MAVLINK_IFLAG_MASK) != 0) {
			// message includes an incompatible feature flag
			_mav_parse_error(status);
			status->msg_received = MAVLINK_FRAMING_INCOMPLETE;
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
			break;
		}
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_INCOMPAT_FLAGS;
		break;

	case MAVLINK_PARSE_STATE_GOT_INCOMPAT_FLAGS:
		rxmsg->compat_flags = c;
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_COMPAT_FLAGS;
		break;

	case MAVLINK_PARSE_STATE_GOT_COMPAT_FLAGS:
		rxmsg->seq = c;
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_SEQ;
		break;
                
	case MAVLINK_PARSE_STATE_GOT_SEQ:
		rxmsg->sysid = c;
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_SYSID;
		break;

	case MAVLINK_PARSE_STATE_GOT_SYSID:
		rxmsg->compid = c;
		mavlink_update_checksum(rxmsg, c);
                status->parse_state = MAVLINK_PARSE_STATE_GOT_COMPID;
		break;

	case MAVLINK_PARSE_STATE_GOT_COMPID:
		rxmsg->msgid = c;
		mavlink_update_checksum(rxmsg, c);
		if (status->flags & MAVLINK_STATUS_FLAG_IN_MAVLINK1) {
			if(rxmsg->len > 0) {
				status->parse_state = MAVLINK_PARSE_STATE_GOT_MSGID3;
			} else {
				status->parse_state = MAVLINK_PARSE_STATE_GOT_PAYLOAD;
			}
#ifdef MAVLINK_CHECK_MESSAGE_LENGTH
			if (rxmsg->len < mavlink_min_message_length(rxmsg) ||
				rxmsg->len > mavlink_max_message_length(rxmsg)) {
				_mav_parse_error(status);
				status->parse_state = MAVLINK_PARSE_STATE_IDLE;
				break;
			}
#endif
		} else {
			status->parse_state = MAVLINK_PARSE_STATE_GOT_MSGID1;
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_MSGID1:
		rxmsg->msgid |= ((uint32_t)c)<<8;
		mavlink_update_checksum(rxmsg, c);
		status->parse_state = MAVLINK_PARSE_STATE_GOT_MSGID2;
		break;

	case MAVLINK_PARSE_STATE_GOT_MSGID2:
		rxmsg->msgid |= ((uint32_t)c)<<16;
		mavlink_update_checksum(rxmsg, c);
		if(rxmsg->len > 0){
			status->parse_state = MAVLINK_PARSE_STATE_GOT_MSGID3;
		} else {
			status->parse_state = MAVLINK_PARSE_STATE_GOT_PAYLOAD;
		}
#ifdef MAVLINK_CHECK_MESSAGE_LENGTH
        if (rxmsg->len < mavlink_min_message_length(rxmsg) ||
            rxmsg->len > mavlink_max_message_length(rxmsg))
        {
			_mav_parse_error(status);
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
			break;
        }
#endif
		break;
                
	case MAVLINK_PARSE_STATE_GOT_MSGID3:
		_MAV_PAYLOAD_NON_CONST(rxmsg)[status->packet_idx++] = (char)c;
		mavlink_update_checksum(rxmsg, c);
		if (status->packet_idx == rxmsg->len)
		{
			status->parse_state = MAVLINK_PARSE_STATE_GOT_PAYLOAD;
		}
		break;

	case MAVLINK_PARSE_STATE_GOT_PAYLOAD: {
		const mavlink_msg_entry_t *e = mavlink_get_msg_entry(rxmsg->msgid);
		if (e == NULL) {
			// Message not found in CRC_EXTRA table.
			status->parse_state = MAVLINK_PARSE_STATE_GOT_BAD_CRC1;
			rxmsg->ck[0] = c;
		} else {
			uint8_t crc_extra = e->crc_extra;
			mavlink_update_checksum(rxmsg, crc_extra);
			if (c != (rxmsg->checksum & 0xFF)) {
				status->parse_state = MAVLINK_PARSE_STATE_GOT_BAD_CRC1;
			} else {
				status->parse_state = MAVLINK_PARSE_STATE_GOT_CRC1;
			}
			rxmsg->ck[0] = c;

			// zero-fill the packet to cope with short incoming packets
				if (e && status->packet_idx < e->max_msg_len) {
					memset(&_MAV_PAYLOAD_NON_CONST(rxmsg)[status->packet_idx], 0, e->max_msg_len - status->packet_idx);
			}
		}
		break;
        }

	case MAVLINK_PARSE_STATE_GOT_CRC1:
	case MAVLINK_PARSE_STATE_GOT_BAD_CRC1:
		if (status->parse_state == MAVLINK_PARSE_STATE_GOT_BAD_CRC1 || c != (rxmsg->checksum >> 8)) {
			// got a bad CRC message
			status->msg_received = MAVLINK_FRAMING_BAD_CRC;
		} else {
			// Successfully got message
			status->msg_received = MAVLINK_FRAMING_OK;
		}
		rxmsg->ck[1] = c;

		if (rxmsg->incompat_flags & MAVLINK_IFLAG_SIGNED) {
			if (status->msg_received == MAVLINK_FRAMING_BAD_CRC) {
			    // If the CRC is already wrong, don't overwrite msg_received,
			    // otherwise we can end up with garbage flagged as valid.
			    status->parse_state = MAVLINK_PARSE_STATE_SIGNATURE_WAIT_BAD_CRC;
			} else {
			    status->parse_state = MAVLINK_PARSE_STATE_SIGNATURE_WAIT;
			    status->msg_received = MAVLINK_FRAMING_INCOMPLETE;
			}
			status->signature_wait = MAVLINK_SIGNATURE_BLOCK_LEN;
		} else {
			if (status->signing &&
			   	(status->signing->accept_unsigned_callback == NULL ||
			   	 !status->signing->accept_unsigned_callback(status, rxmsg->msgid))) {

				// If the CRC is already wrong, don't overwrite msg_received.
				if (status->msg_received != MAVLINK_FRAMING_BAD_CRC) {
					status->msg_received = MAVLINK_FRAMING_BAD_SIGNATURE;
				}
			}
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
			if (r_message != NULL) {
				memcpy(r_message, rxmsg, sizeof(mavlink_message_t));
			}
		}
		break;
	case MAVLINK_PARSE_STATE_SIGNATURE_WAIT:
	case MAVLINK_PARSE_STATE_SIGNATURE_WAIT_BAD_CRC:
		rxmsg->signature[MAVLINK_SIGNATURE_BLOCK_LEN-status->signature_wait] = c;
		status->signature_wait--;
		if (status->signature_wait == 0) {
			// we have the whole signature, check it is OK
#ifndef MAVLINK_NO_SIGNATURE_CHECK
			bool sig_ok = mavlink_signature_check(status->signing, status->signing_streams, rxmsg);
#else
			bool sig_ok = true;
#endif
			if (!sig_ok &&
			   	(status->signing->accept_unsigned_callback &&
			   	 status->signing->accept_unsigned_callback(status, rxmsg->msgid))) {
				// accepted via application level override
				sig_ok = true;
			}
			if (status->parse_state == MAVLINK_PARSE_STATE_SIGNATURE_WAIT_BAD_CRC) {
			    status->msg_received = MAVLINK_FRAMING_BAD_CRC;
			} else if (sig_ok) {
			    status->msg_received = MAVLINK_FRAMING_OK;
			} else {
			    status->msg_received = MAVLINK_FRAMING_BAD_SIGNATURE;
			}
			status->parse_state = MAVLINK_PARSE_STATE_IDLE;
			if (r_message !=NULL) {
				memcpy(r_message, rxmsg, sizeof(mavlink_message_t));
			}
		}
		break;
	}

	// If a message has been successfully decoded, check index
	if (status->msg_received == MAVLINK_FRAMING_OK)
	{
		//while(status->current_seq != rxmsg->seq)
		//{
		//	status->packet_rx_drop_count++;
		//               status->current_seq++;
		//}
		status->current_rx_seq = rxmsg->seq;
		// Initial condition: If no packet has been received so far, drop count is undefined
		if (status->packet_rx_success_count == 0) status->packet_rx_drop_count = 0;
		// Count this packet as received
		status->packet_rx_success_count++;
	}

       if (r_message != NULL) {
           r_message->len = rxmsg->len; // Provide visibility on how far we are into current msg
       }
       if (r_mavlink_status != NULL) {	
           r_mavlink_status->parse_state = status->parse_state;
           r_mavlink_status->packet_idx = status->packet_idx;
           r_mavlink_status->current_rx_seq = status->current_rx_seq+1;
           r_mavlink_status->packet_rx_success_count = status->packet_rx_success_count;
           r_mavlink_status->packet_rx_drop_count = status->parse_error;
           r_mavlink_status->flags = status->flags;
       }
       status->parse_error = 0;

	if (status->msg_received == MAVLINK_FRAMING_BAD_CRC) {
		/*
		  the CRC came out wrong. We now need to overwrite the
		  msg CRC with the one on the wire so that if the
		  caller decides to forward the message anyway that
		  mavlink_msg_to_send_buffer() won't overwrite the
		  checksum
		 */
            if (r_message != NULL) {
                r_message->checksum = rxmsg->ck[0] | (rxmsg->ck[1]<<8);
            }
	}

	return status->msg_received;
}

/**
 * @brief MAVLink字符解析函数（便捷版本）
 * 
 * 该函数逐个字节解析MAVLink消息，一旦成功解码出完整数据包即返回。
 * 返回值：0（未解码出消息），1（成功解码且CRC正确），2（CRC错误）。
 * 
 * 消息被解析到内部缓冲区（每个通道一个）。当接收到完整消息时，
 * 数据被复制到*r_message，通道状态被复制到*r_mavlink_status。
 * 
 * @param[in] chan 要解析的通道ID（逻辑分区，非物理通道）
 * @param[in] c 要解析的字符
 * @param[out] r_message 解码后的消息数据，如果失败则为NULL
 * @param[out] r_mavlink_status 通道的统计信息
 * @return 解析结果代码
 * 
 * 典型使用场景：
 * @code
 * #include <mavlink.h>
 * mavlink_status_t status;
 * mavlink_message_t msg;
 * int chan = 0;
 * while(serial.bytesAvailable > 0) {
 *   uint8_t byte = serial.getNextByte();
 *   if (mavlink_frame_char(chan, byte, &msg, &status) != MAVLINK_FRAMING_INCOMPLETE) {
 *     printf("收到消息ID %d，序列号：%d，来自系统 %d 的组件 %d", 
 *            msg.msgid, msg.seq, msg.sysid, msg.compid);
 *   }
 * }
 * @endcode
 */
MAVLINK_HELPER uint8_t mavlink_frame_char(uint8_t chan, uint8_t c, mavlink_message_t* r_message, mavlink_status_t* r_mavlink_status)
{
	return mavlink_frame_char_buffer(mavlink_get_channel_buffer(chan),
					 mavlink_get_channel_status(chan),
					 c,
					 r_message,
					 r_mavlink_status);
}

/**
 * @brief 设置协议版本
 * 
 * 为指定通道设置MAVLink协议版本（1或2）。
 * 
 * @param[in] chan 通道ID
 * @param[in] version 协议版本号（1或2）
 */
MAVLINK_HELPER void mavlink_set_proto_version(uint8_t chan, unsigned int version)
{
	mavlink_status_t *status = mavlink_get_channel_status(chan);
	if (version > 1) {
		status->flags &= ~(MAVLINK_STATUS_FLAG_OUT_MAVLINK1);
	} else {
		status->flags |= MAVLINK_STATUS_FLAG_OUT_MAVLINK1;
	}
}

/**
 * @brief 获取协议版本
 * 
 * 查询指定通道的MAVLink协议版本。
 * 
 * @param[in] chan 通道ID
 * @return 1表示v1协议，2表示v2协议
 */
MAVLINK_HELPER unsigned int mavlink_get_proto_version(uint8_t chan)
{
	mavlink_status_t *status = mavlink_get_channel_status(chan);
	if ((status->flags & MAVLINK_STATUS_FLAG_OUT_MAVLINK1) > 0) {
		return 1;
	} else {
		return 2;
	}
}

/**
 * @brief MAVLink字符解析函数（简化版本）
 * 
 * 该函数逐个字节解析MAVLink消息，一旦成功解码出完整数据包即返回。
 * 返回值：0（解码失败或CRC错误），1（成功解码且CRC正确）。
 * 
 * 消息被解析到内部缓冲区（每个通道一个）。当接收到完整消息时，
 * 数据被复制到*r_message，通道状态被复制到*r_mavlink_status。
 * 
 * @param[in] chan 要解析的通道ID（逻辑分区，非物理通道）
 * @param[in] c 要解析的字符
 * @param[out] r_message 解码后的消息数据，如果失败则为NULL
 * @param[out] r_mavlink_status 通道的统计信息
 * @return 解析结果代码
 * 
 * 典型使用场景：
 * @code
 * #include <mavlink.h>
 * mavlink_status_t status;
 * mavlink_message_t msg;
 * int chan = 0;
 * while(serial.bytesAvailable > 0) {
 *   uint8_t byte = serial.getNextByte();
 *   if (mavlink_parse_char(chan, byte, &msg, &status)) {
 *     printf("收到消息ID %d，序列号：%d，来自系统 %d 的组件 %d", 
 *            msg.msgid, msg.seq, msg.sysid, msg.compid);
 *   }
 * }
 * @endcode
 */
MAVLINK_HELPER uint8_t mavlink_parse_char(uint8_t chan, uint8_t c, mavlink_message_t* r_message, mavlink_status_t* r_mavlink_status)
{
    uint8_t msg_received = mavlink_frame_char(chan, c, r_message, r_mavlink_status);
    if (msg_received == MAVLINK_FRAMING_BAD_CRC ||
	msg_received == MAVLINK_FRAMING_BAD_SIGNATURE) {
	    // we got a bad CRC. Treat as a parse failure
	    mavlink_message_t* rxmsg = mavlink_get_channel_buffer(chan);
	    mavlink_status_t* status = mavlink_get_channel_status(chan);
	    _mav_parse_error(status);
	    status->msg_received = MAVLINK_FRAMING_INCOMPLETE;
	    status->parse_state = MAVLINK_PARSE_STATE_IDLE;
	    if (c == MAVLINK_STX)
	    {
		    status->parse_state = MAVLINK_PARSE_STATE_GOT_STX;
		    rxmsg->len = 0;
		    mavlink_start_checksum(rxmsg);
	    }
	    return 0;
    }
    return msg_received;
}

/**
 * @brief 将1-32位的位域放入缓冲区
 * 
 * 该函数将指定长度的位域值编码到缓冲区中，支持跨字节存储。
 * 
 * @param[in] b 要编码的值
 * @param[in] bits 使用的位数（1-32）
 * @param[in] packet_index 数据包中的起始字节索引
 * @param[in] bit_index 字节中的起始位索引（0-7）
 * @param[out] r_bit_index 更新后的位索引
 * @param[in,out] buffer 目标缓冲区
 * @return 最后使用的字节在缓冲区中的新位置
 */
MAVLINK_HELPER uint8_t put_bitfield_n_by_index(int32_t b, uint8_t bits, uint8_t packet_index, uint8_t bit_index, uint8_t* r_bit_index, uint8_t* buffer)
{
	uint16_t bits_remain = bits;
	// Transform number into network order
	int32_t v;
	uint8_t i_bit_index, i_byte_index, curr_bits_n;
#if MAVLINK_NEED_BYTE_SWAP
	union {
		int32_t i;
		uint8_t b[4];
	} bin, bout;
	bin.i = b;
	bout.b[0] = bin.b[3];
	bout.b[1] = bin.b[2];
	bout.b[2] = bin.b[1];
	bout.b[3] = bin.b[0];
	v = bout.i;
#else
	v = b;
#endif

	// buffer in
	// 01100000 01000000 00000000 11110001
	// buffer out
	// 11110001 00000000 01000000 01100000

	// Existing partly filled byte (four free slots)
	// 0111xxxx

	// Mask n free bits
	// 00001111 = 2^0 + 2^1 + 2^2 + 2^3 = 2^n - 1
	// = ((uint32_t)(1 << n)) - 1; // = 2^n - 1

	// Shift n bits into the right position
	// out = in >> n;

	// Mask and shift bytes
	i_bit_index = bit_index;
	i_byte_index = packet_index;
	if (bit_index > 0)
	{
		// If bits were available at start, they were available
		// in the byte before the current index
		i_byte_index--;
	}

	// While bits have not been packed yet
	while (bits_remain > 0)
	{
		// Bits still have to be packed
		// there can be more than 8 bits, so
		// we might have to pack them into more than one byte

		// First pack everything we can into the current 'open' byte
		//curr_bits_n = bits_remain << 3; // Equals  bits_remain mod 8
		//FIXME
		if (bits_remain <= (uint8_t)(8 - i_bit_index))
		{
			// Enough space
			curr_bits_n = (uint8_t)bits_remain;
		}
		else
		{
			curr_bits_n = (8 - i_bit_index);
		}
		
		// Pack these n bits into the current byte
		// Mask out whatever was at that position with ones (xxx11111)
		buffer[i_byte_index] &= (0xFF >> (8 - curr_bits_n));
		// Put content to this position, by masking out the non-used part
		buffer[i_byte_index] |= ((0x00 << curr_bits_n) & v);
		
		// Increment the bit index
		i_bit_index += curr_bits_n;

		// Now proceed to the next byte, if necessary
		bits_remain -= curr_bits_n;
		if (bits_remain > 0)
		{
			// Offer another 8 bits / one byte
			i_byte_index++;
			i_bit_index = 0;
		}
	}
	
	*r_bit_index = i_bit_index;
	// If a partly filled byte is present, mark this as consumed
	if (i_bit_index != 7) i_byte_index++;
	return i_byte_index - packet_index;
}

#ifdef MAVLINK_USE_CONVENIENCE_FUNCTIONS

/* 
 * 要使MAVLink在您的MCU上工作，可以定义以下之一：
 * 1. comm_send_ch() - 逐字节发送
 * 2. MAVLINK_SEND_UART_BYTES() - 整包发送
 *
 * 示例实现：
 *
 * #include "mavlink_types.h"
 * void comm_send_ch(mavlink_channel_t chan, uint8_t ch) {
 *     if (chan == MAVLINK_COMM_0) uart0_transmit(ch);
 *     if (chan == MAVLINK_COMM_1) uart1_transmit(ch);
 * }
 */

/**
 * @brief 通过UART发送数据
 * 
 * 内部函数，用于通过指定通道发送数据。
 * 根据平台定义选择逐字节发送或整包发送。
 * 
 * @param[in] chan 通道ID
 * @param[in] buf 要发送的数据缓冲区
 * @param[in] len 数据长度
 */
MAVLINK_HELPER void _mavlink_send_uart(mavlink_channel_t chan, const char *buf, uint16_t len)
{
#ifdef MAVLINK_SEND_UART_BYTES
	/* 这是更高效的方法，如果平台已定义 */
	MAVLINK_SEND_UART_BYTES(chan, (const uint8_t *)buf, len);
#else
	/* 回退到逐字节发送 */
	uint16_t i;
	for (i = 0; i < len; i++) {
		comm_send_ch(chan, (uint8_t)buf[i]);
	}
#endif
}
#endif // MAVLINK_USE_CONVENIENCE_FUNCTIONS

#ifdef MAVLINK_USE_CXX_NAMESPACE
} // namespace mavlink
#endif
