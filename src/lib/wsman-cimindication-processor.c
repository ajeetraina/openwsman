/*******************************************************************************
* Copyright (C) 2004-2007 Intel Corp. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
*  - Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
*  - Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
*  - Neither the name of Intel Corp. nor the names of its
*    contributors may be used to endorse or promote products derived from this
*    software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL Intel Corp. OR THE CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/**
 * @author Liang Hou
 */
#include "wsman-faults.h"
#include "wsman-soap.h"
#include "wsman-names.h"
#include "wsman-soap-message.h"
#include "wsman-soap-envelope.h"
#include "wsman-xml-api.h"
#include "wsman-xml.h"
#include "wsman-event-pool.h"
#include "wsman-cimindication-processor.h"


static int isvalidCIMIndicationExport(WsXmlDocH doc){
	if(doc == NULL) return 0;
	WsXmlNodeH node = ws_xml_get_doc_root(doc);
	WsXmlNodeH temp = NULL;
	node = ws_xml_get_child(node, 0, NULL, CIMXML_MESSAGE);
	temp = ws_xml_get_child(node, 0, NULL, CIMXML_SIMPLEEXPREQ);
	if(temp == NULL) {
		temp = ws_xml_get_child(node, 0, NULL, CIMXML_MULTIEXPREQ);
		if(temp == NULL) return 0;
	}
	return 1;
}

static void cimxml_build_response_msg(WsXmlDocH indoc, WsXmlDocH *outdoc) {
	WsXmlDocH doc = NULL;
	WsXmlNodeH outnode = NULL;
	WsXmlNodeH innode = NULL;
	WsXmlNodeH temp = NULL;
	WsXmlNodeH temp2 = NULL;
	doc = ws_xml_create_doc(NULL, CIMXML_CIM);
	outnode = ws_xml_get_doc_root(doc);
	innode = ws_xml_get_doc_root(indoc);
//	char *value = ws_xml_get_node_attr(WsXmlNodeH node, int index)
	ws_xml_add_node_attr(outnode, NULL, CIMXML_CIMVERSION, "2.0");
	ws_xml_add_node_attr(outnode, NULL, CIMXML_DTDVERSION, "2.0");
	outnode = ws_xml_add_child(outnode, NULL, CIMXML_MESSAGE, NULL);
	innode = ws_xml_get_child(innode, 0, NULL, CIMXML_MESSAGE);
	int n = ws_xml_get_node_attr_count(innode);
	int i = 0;
	while(i < n) {
		WsXmlAttrH attr = ws_xml_get_node_attr(innode, i);
	 	char *name = ws_xml_get_attr_name(attr);
	 	char *value = ws_xml_get_attr_value(attr);
	 	ws_xml_add_node_attr(outnode, NULL, name, value);
	 	i++;
	}
	temp = ws_xml_get_child(innode, 0, NULL, CIMXML_SIMPLEEXPREQ);
	if(temp) {
		outnode = ws_xml_add_child(outnode, NULL, CIMXML_SIMPLEEXPRSP, NULL);
		outnode = ws_xml_add_child(outnode, NULL, CIMXML_EXPMETHODRESPONSE, NULL);
		ws_xml_add_node_attr(outnode, NULL, CIMXML_NAME, "ExportIndication");
		ws_xml_add_child(outnode, NULL, CIMXML_IRETURNVALUE, NULL);
	}
	else {
		temp = ws_xml_get_child(innode, 0, NULL, CIMXML_MULTIEXPREQ);
		outnode = ws_xml_add_child(outnode, NULL, CIMXML_MULTIEXPRSQ, NULL);
		n = ws_xml_get_child_count(temp);
		i = 0;
		while(i < n) {
			innode = ws_xml_get_child(temp, i, NULL, CIMXML_SIMPLEEXPREQ);
			temp2 = ws_xml_add_child(outnode, NULL, CIMXML_EXPMETHODRESPONSE, NULL);
			ws_xml_add_node_attr(temp2, NULL, CIMXML_NAME, "ExportIndication");
			ws_xml_add_child(temp2, NULL, CIMXML_IRETURNVALUE, NULL);
			i++;
		}
	}
	*outdoc = doc;
}

static
char * get_cim_indication_namespace(WsSubscribeInfo *subsInfo, char *classname) {
	hscan_t hs;
	hnode_t *hn;
	char *sub;
	if (strstr(subsInfo->uri, XML_NS_CIM_CLASS) != NULL) {
	   	return u_strdup(subsInfo->uri);
	}
	hash_t *namespaces = subsInfo->vendor_namespaces;
	if (namespaces) {
		hash_scan_begin(&hs, namespaces);
		while ((hn = hash_scan_next(&hs))) {
			debug("namespace=%s", (char *) hnode_get(hn));
			if ((sub =  strstr(classname, (char *) hnode_getkey(hn)))) {
				return u_strdup((char *)hnode_get(hn));
			}
		}
	}
	return NULL;
}

static 
WsNotificationInfoH create_notification_entity(WsSubscribeInfo *subsInfo, WsXmlNodeH node){
	char *classname = NULL;
	char *class_namespace = NULL;
	WsXmlNodeH indicationnode = NULL;
	WsNotificationInfoH notificationinfo = NULL;
	notificationinfo = u_zalloc(sizeof(*notificationinfo));
	WsXmlNodeH instance = ws_xml_get_child(node, 0, NULL, CIMXML_EXPMETHODCALL);
	if(instance == NULL) return NULL;
	instance = ws_xml_get_child(instance, 0, NULL, CIMXML_EXPPARAMVALUE);
	if(instance == NULL) return NULL;
	instance = ws_xml_get_child(instance, 0, NULL, CIMXML_INSTANCE);
	if(instance == NULL) return NULL;
	WsXmlAttrH attr = ws_xml_find_node_attr(instance, NULL, CIMXML_CLASSNAME);
	if(attr) {
		classname = ws_xml_get_attr_value(attr);
		class_namespace = get_cim_indication_namespace(subsInfo, classname);
		notificationinfo->EventAction = u_strdup_printf("%s/%s",class_namespace, classname);
	}
	notificationinfo->EventContent = ws_xml_create_doc(notificationinfo->EventAction, classname);
	indicationnode = ws_xml_get_doc_root(notificationinfo->EventContent);
	int m = ws_xml_get_child_count(instance);
	int n = 0;
	while(n < m) {
		//only parse tag "Property" temporarily
		node = ws_xml_get_child(instance, n, NULL, CIMXML_PROPERTY);
		attr = ws_xml_find_node_attr(node, NULL, CIMXML_NAME);
		char *property = NULL;
		char *value = NULL;
		if(attr) {
			property = ws_xml_get_attr_value(attr);
		}
		value = ws_xml_get_node_text(node);
		ws_xml_add_child(indicationnode, notificationinfo->EventAction, property, value);
		n++;
	}
	if(class_namespace)
		u_free(class_namespace);
	return notificationinfo;
}


static 
void create_indication_event(WsXmlDocH indoc, WsSubscribeInfo *subsInfo, EventPoolOpSetH opset) {
	int count, i;
	int retval;
	WsNotificationInfoH notificationinfo = NULL;
	WsXmlNodeH node = ws_xml_get_doc_root(indoc);
	node = ws_xml_get_child(node, 0, NULL, CIMXML_MESSAGE);
	WsXmlNodeH tmp = ws_xml_get_child(node, 0, NULL, CIMXML_MULTIEXPREQ);
	if(tmp) {
		count = ws_xml_get_child_count(tmp);
		i = 0;
		while(i < count) {
			node = ws_xml_get_child(tmp, i,  NULL, CIMXML_SIMPLEEXPREQ);
			notificationinfo = create_notification_entity(subsInfo, node);
			if(notificationinfo == NULL) {
				i++;
				continue;
			}
			if(subsInfo->deliveryMode == WS_EVENT_DELIVERY_MODE_PULL)
				retval = opset->addpull(subsInfo->subsId, notificationinfo);
			else
				retval = opset->add(subsInfo->subsId, notificationinfo);
			if(retval) {
				u_free(notificationinfo->EventAction);
				ws_xml_destroy_doc(notificationinfo->EventContent);
				u_free(notificationinfo);
			}
			i++;
		}
		
	}
	else {
		tmp = ws_xml_get_child(node, 0, NULL, CIMXML_SIMPLEEXPREQ);
		notificationinfo = create_notification_entity(subsInfo, tmp);
		if(subsInfo->deliveryMode == WS_EVENT_DELIVERY_MODE_PULL)
			retval = opset->addpull(subsInfo->subsId, notificationinfo);
		else
			retval = opset->add(subsInfo->subsId, notificationinfo);
		if(retval) {
			u_free(notificationinfo->EventAction);
			ws_xml_destroy_doc(notificationinfo->EventContent);
			u_free(notificationinfo);
		}
	}
	
}

void CIM_Indication_call(cimxml_context *cntx, WsmanMessage *message, void *opaqueData) {
	char *response = NULL;
	int len;
	WsXmlDocH indicationRequest = NULL;
	WsXmlDocH indicationResponse = NULL;
	SoapH soap = cntx->soap;
	char *servicepath = cntx->servicepath;
	indicationRequest = ws_xml_read_memory(u_buf_ptr(message->request), u_buf_len(message->request), 
		"UTF-8", 0);
	if(indicationRequest == NULL) {
		debug("error, request cannot be parsed !");
		wsman_set_fault(message, CIMXML_STATUS_REQUEST_NOT_VALID, 0, NULL);
		return;
	}
	if(!isvalidCIMIndicationExport(indicationRequest)) {
		debug("error, invalid cim indication");
		wsman_set_fault(message, CIMXML_STATUS_UNSUPPORTED_OPERATION, 0, NULL);
		return;
	}
	//to do here: put indication in event pool
	WsSubscribeInfo *subsInfo = NULL;
	char *uuid = strrchr(servicepath, '/') + 1;
	list_t *subslist = soap->subscriptionMemList;
	lnode_t *node = list_first(subslist);
	while(node) {
		subsInfo = (WsSubscribeInfo *)node->list_data;
		if(!strcmp(subsInfo->subsId, uuid)) break;
		node = list_next(subslist, node);
	}
	if(node == NULL) {
		message->http_code = WSMAN_STATUS_NOT_FOUND;
		goto DONE;
	}
	EventPoolOpSetH opset = soap->eventpoolOpSet;
	create_indication_event(indicationRequest, subsInfo, opset);
	cimxml_build_response_msg(indicationRequest, &indicationResponse);
	ws_xml_dump_memory_enc(indicationResponse, &response, &len, "utf-8");
	u_buf_construct(message->response, response, len, len);
	message->http_code = WSMAN_STATUS_OK;
DONE:
	ws_xml_destroy_doc(indicationRequest);
	ws_xml_destroy_doc(indicationResponse);
}
