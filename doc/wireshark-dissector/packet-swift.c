/* packet-swift.c
 * Routines for swift protocol packet disassembly
 * By Andrew Keating <andrewzkeating@gmail.com>
 * Copyright 2011 Andrew Keating
 *
 * $Id$
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <epan/packet.h>

static int proto_swift = -1;

/* Global fields */
static int hf_swift_receiving_channel = -1;
static int hf_swift_message_type = -1;

/* 00 Handshake fields */
static int hf_swift_handshake_channel = -1;

/* 01 Data fields */
static int hf_swift_data_bin_id = -1;
static int hf_swift_data_payload = -1;

/* 02 Ack fields */
static int hf_swift_ack_bin_id = -1;
static int hf_swift_ack_timestamp = -1;

/* 03 Have fields */
static int hf_swift_have_bin_id = -1;

/* 04 Hash fields */
static int hf_swift_hash_bin_id = -1;
static int hf_swift_hash_value = -1;

/* 05 PEX+ fields */
static int hf_swift_pexplus_ip = -1;
static int hf_swift_pexplus_port = -1;

/* 06 PEX- fields */
static int hf_swift_pexminus_ip = -1;
static int hf_swift_pexminus_port = -1;

/* 07 Signed hash fields */
static int hf_swift_signed_hash_bin_id = -1;
static int hf_swift_signed_hash_value = -1;
static int hf_swift_signed_hash_signature = -1;

/* 08 Hint fields */
static int hf_swift_hint_bin_id = -1;

static gint ett_swift = -1;

static void dissect_swift(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree);
static gboolean dissect_swift_heur(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree);

static const value_string message_type_names[] = {
    { 0, "Handshake" },
    { 1, "Data" },
    { 2, "Ack" },
    { 3, "Have" },
    { 4, "Hash" },
    { 5, "PEX+" },
    { 6, "PEX-" },
    { 7, "Signed Hash" },
    { 8, "Hint" },
    { 9, "SWIFT_MSGTYPE_RCVD" },
    { 10, "SWIFT_MESSAGE_COUNT" },
    { 0, NULL}
};


void
proto_register_swift(void)
{
    static hf_register_info hf[] = {
	/* Global */
    { &hf_swift_receiving_channel,
		{ "Receiving Channel", "swift.receiving.channel",
		FT_UINT32, BASE_HEX,
		NULL, 0x0,
		NULL, HFILL }
     },
	{ &hf_swift_message_type,
	    { "Message Type", "swift.message.type",
        FT_UINT8, BASE_DEC,
        VALS(message_type_names), 0x0,
        NULL, HFILL }
	},

	/* 00 Handshake */
	{ &hf_swift_handshake_channel,
	    { "Handshake Channel", "swift.handshake.channel",
	    FT_UINT32, BASE_HEX,
        NULL, 0x0,
        NULL, HFILL }
	},

	/* 01 Data */
	{ &hf_swift_data_bin_id,
	    { "Data Bin ID", "swift.data.bin_id",
	    FT_UINT32, BASE_HEX,
	    NULL, 0x0,
	    NULL, HFILL }
	},
	{ &hf_swift_data_payload,
	    { "Data Payload", "swift.data.payload",
	    FT_BYTES, BASE_NONE,
	    NULL, 0x0,
	    NULL, HFILL }
	},

	/* 02 Ack */
	{ &hf_swift_ack_bin_id,
	    { "Ack Bin ID", "swift.ack.bin_id",
	    FT_UINT32, BASE_HEX,
	    NULL, 0x0,
	    NULL, HFILL }
	},
	{ &hf_swift_ack_timestamp,
	    { "Timestamp", "swift.ack.timestamp",
	    FT_UINT64, BASE_HEX,
	    NULL, 0x0,
	    NULL, HFILL }
	},

	/* 03 Have */
	{ &hf_swift_have_bin_id,
	    { "Have Bin ID", "swift.have.bin_id",
	    FT_UINT32, BASE_HEX,
	    NULL, 0x0,
	    NULL, HFILL }
	},

	/* 04 Hash */ 
	{ &hf_swift_hash_bin_id,
	    { "Hash Bin ID", "swift.hash.bin_id",
	    FT_UINT32, BASE_HEX,
	    NULL, 0x0,
	    NULL, HFILL }
	},
	{ &hf_swift_hash_value,
	    { "Hash Value", "swift.hash.value",
	    FT_BYTES, BASE_NONE,
	    NULL, 0x0,
	    NULL, HFILL }
	},

	/* 05 PEX+ */
	{ &hf_swift_pexplus_ip,
	    { "PEX+ IP Address", "swift.pex_plus.ip",
	    FT_IPv4, BASE_NONE,
	    NULL, 0x0,
	    NULL, HFILL }
	},
	{ &hf_swift_pexplus_port,
	    { "PEX+ Port", "swift.pex_plus.port",
	    FT_UINT16, BASE_DEC,
	    NULL, 0x0,
	    NULL, HFILL }
	},

	/* 06 PEX- */
	{ &hf_swift_pexminus_ip,
	    { "PEX- IP Address", "swift.pex_minus.ip",
	    FT_IPv4, BASE_NONE,
	    NULL, 0x0,
	    NULL, HFILL }
	},
	{ &hf_swift_pexminus_port,
	    { "PEX- Port", "swift.pex_minus.port",
	    FT_UINT16, BASE_DEC,
	    NULL, 0x0,
	    NULL, HFILL }
	},

	/* 07 Signed Hash */
	{ &hf_swift_signed_hash_bin_id,
	    { "Signed Hash Bin ID", "swift.signed_hash.bin_id",
	    FT_UINT32, BASE_HEX,
	    NULL, 0x0,
	    NULL, HFILL }
	},
	{ &hf_swift_signed_hash_value,
	    { "Signed Hash Value", "swift.signed_hash.value",
	    FT_BYTES, BASE_NONE,
	    NULL, 0x0,
	    NULL, HFILL }
	},
	{ &hf_swift_signed_hash_signature,
	    { "Signed Hash Signature", "swift.signed_hash.signature",
	    FT_BYTES, BASE_NONE,
	    NULL, 0x0,
	    NULL, HFILL }
	},

	/* 08 Hint */
	{ &hf_swift_hint_bin_id,
	    { "Hint Bin ID", "swift.hint.bin_id",
	    FT_UINT32, BASE_HEX,
	    NULL, 0x0,
	    NULL, HFILL }
	},
   };

    /* Setup protocol subtree array */
    static gint *ett[] = {
        &ett_swift
    };

    proto_swift = proto_register_protocol (
        "swift: the multiparty transport protocol", /* name       */
        "swift",      /* short name */
        "swift"       /* abbrev     */
        );

    proto_register_field_array(proto_swift, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
	register_dissector("swift", dissect_swift, proto_swift);
}

void
proto_reg_handoff_swift(void)
{
	dissector_handle_t swift_handle;
	swift_handle = find_dissector("swift");

	/* Allow "Decode As" with any UDP packet. */
	dissector_add_handle("udp.port", swift_handle);

	/* Add our heuristic packet finder. */
	heur_dissector_add("udp", dissect_swift_heur, proto_swift);
}

/* This heuristic is somewhat ambiguous, but for research purposes, it should be fine */
static gboolean
dissect_swift_heur(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	guint message_length;
	message_length = tvb_length(tvb);
	/* If the fifth byte isn't one of the supported packet types, it's not swift (except keep-alives) */
	if(message_length != 4) {	
		guint8 message_type;
		message_type = tvb_get_guint8(tvb, 4);
		if(message_type > 10) {
			return FALSE;
		}
	}

	dissect_swift(tvb, pinfo, tree);
	return TRUE;
}

static void
dissect_swift(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree)
{
	gint offset = 0;
    col_set_str(pinfo->cinfo, COL_PROTOCOL, "swift");
    /* Clear out stuff in the info column */
    col_clear(pinfo->cinfo,COL_INFO);

    if (tree) { /* we are being asked for details */
        proto_item *ti;
		ti = proto_tree_add_item(tree, proto_swift, tvb, 0, -1, FALSE);

		proto_tree *swift_tree;
		swift_tree = proto_item_add_subtree(ti, ett_swift);

		/* All messages start with the receiving channel, so we can pull it out here */
		proto_tree_add_item(swift_tree, hf_swift_receiving_channel, tvb, offset, 4, FALSE); offset += 4;

		/* Loop until there is nothing left to read in the packet */
		while(tvb_bytes_exist(tvb, offset, 1)) {
			guint8 message_type;
			guint dat_len;
		    message_type = tvb_get_guint8(tvb, offset);
			proto_tree_add_item(swift_tree, hf_swift_message_type, tvb, offset, 1, FALSE); 
			offset += 1;

			/* Add message type to the info column */
			if(offset > 5) {
				col_append_fstr(pinfo->cinfo, COL_INFO, ", ");
			}
			col_append_fstr(pinfo->cinfo, COL_INFO, "%s",
				val_to_str(message_type, message_type_names, "Unknown (0x%02x)"));

			/* Add it to the dissection window as well */
			proto_item_append_text(ti, ", %s",
				val_to_str(message_type, message_type_names, "Unknown (0x%02x)"));

			switch(message_type) {
				case 0: /* Handshake */
					proto_tree_add_item(swift_tree, hf_swift_handshake_channel, tvb, offset, 4, FALSE); 
					offset += 4;
					break;
				case 1: /* Data */
					proto_tree_add_item(swift_tree, hf_swift_data_bin_id, tvb, offset, 4, FALSE); 
					offset += 4;
					/* We assume that the data field comprises the rest of this packet */
					dat_len = tvb_length(tvb) - offset;
					proto_tree_add_item(swift_tree, hf_swift_data_payload, tvb, offset, dat_len, FALSE); 
					offset += dat_len;
					break;
				case 2: /* Ack */
					proto_tree_add_item(swift_tree, hf_swift_ack_bin_id, tvb, offset, 4, FALSE); 
					offset += 4;
					proto_tree_add_item(swift_tree, hf_swift_ack_timestamp, tvb, offset, 8, FALSE); 
					offset += 8;
					break;
				case 3: /* Have */
					proto_tree_add_item(swift_tree, hf_swift_have_bin_id, tvb, offset, 4, FALSE); 
					offset += 4;
					break;
				case 4: /* Hash */
					proto_tree_add_item(swift_tree, hf_swift_hash_bin_id, tvb, offset, 4, FALSE); 
					offset += 4;
					proto_tree_add_item(swift_tree, hf_swift_hash_value, tvb, offset, 20, FALSE); 
					offset += 20;
					break;
				case 5: /* PEX+ */
					proto_tree_add_item(swift_tree, hf_swift_pexplus_ip, tvb, offset, 4, FALSE); 
					offset += 4;
					proto_tree_add_item(swift_tree, hf_swift_pexplus_port, tvb, offset, 2, FALSE); 
					offset += 2;
					break;
				case 6: /* PEX- */
					proto_tree_add_item(swift_tree, hf_swift_pexminus_ip, tvb, offset, 4, FALSE); 
					offset += 4;
					proto_tree_add_item(swift_tree, hf_swift_pexminus_port, tvb, offset, 2, FALSE); 
					offset += 2;
					break;
				case 7: /* Signed Hash */
					proto_tree_add_item(swift_tree, hf_swift_signed_hash_bin_id, tvb, offset, 4, FALSE); 
					offset += 4;
					proto_tree_add_item(swift_tree, hf_swift_signed_hash_value, tvb, offset, 20, FALSE); 
					offset += 20;
					/* It is not entirely clear what size the public key will be, so we allow any size
					   For this to work, we must assume there aren't any more messages in the packet */
					dat_len = tvb_length(tvb) - offset;
					proto_tree_add_item(swift_tree, hf_swift_signed_hash_signature, tvb, offset, dat_len, FALSE); 
					offset += dat_len;
					break;
				case 8: /* Hint */
					proto_tree_add_item(swift_tree, hf_swift_hint_bin_id, tvb, offset, 4, FALSE); 
					offset += 4;
					break;
				case 9: /* SWIFT_MSGTYPE_RCVD */
					break;
				case 10: /* SWIFT_MESSAGE_COUNT */
					break;
				default:
					break;
			}
   		}
		/* If the offset is still 4 here, the message is a keep-alive */
		if(offset == 4) {
			col_append_fstr(pinfo->cinfo, COL_INFO, "Keep-Alive");
			proto_item_append_text(ti, ", Keep-Alive");
		}
	}
}

