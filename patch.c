diff --git a/drivers/usb/host/xhci-ring.c b/drivers/usb/host/xhci-ring.c
index fde0277..4ba6974 100644
--- a/drivers/usb/host/xhci-ring.c
+++ b/drivers/usb/host/xhci-ring.c
@@ -3167,9 +3167,11 @@ static int queue_bulk_sg_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 	struct xhci_td *td;
 	struct scatterlist *sg;
 	int num_sgs;
-	int trb_buff_len, this_sg_len, running_total;
+	int trb_buff_len, this_sg_len, running_total, ret;
 	unsigned int total_packet_count;
+	bool zero_length_needed;
 	bool first_trb;
+	int last_trb_num;
 	u64 addr;
 	bool more_trbs_coming;
 
@@ -3185,13 +3187,27 @@ static int queue_bulk_sg_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 	total_packet_count = DIV_ROUND_UP(urb->transfer_buffer_length,
 			usb_endpoint_maxp(&urb->ep->desc));
 
-	trb_buff_len = prepare_transfer(xhci, xhci->devs[slot_id],
+	ret = prepare_transfer(xhci, xhci->devs[slot_id],
 			ep_index, urb->stream_id,
 			num_trbs, urb, 0, mem_flags);
-	if (trb_buff_len < 0)
-		return trb_buff_len;
+	if (ret < 0)
+		return ret;
 
 	urb_priv = urb->hcpriv;
+
+	/* Deal with URB_ZERO_PACKET - need one more td/trb */
+	zero_length_needed = urb->transfer_flags & URB_ZERO_PACKET &&
+		urb_priv->length == 2;
+	if (zero_length_needed) {
+		num_trbs++;
+		xhci_dbg(xhci, "Creating zero length td.\n");
+		ret = prepare_transfer(xhci, xhci->devs[slot_id],
+				ep_index, urb->stream_id,
+				1, urb, 1, mem_flags);
+		if (ret < 0)
+			return ret;
+	}
+
 	td = urb_priv->td[0];
 
 	/*
@@ -3221,6 +3237,7 @@ static int queue_bulk_sg_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		trb_buff_len = urb->transfer_buffer_length;
 
 	first_trb = true;
+	last_trb_num = zero_length_needed ? 2 : 1;
 	/* Queue the first TRB, even if it's zero-length */
 	do {
 		u32 field = 0;
@@ -3238,12 +3255,15 @@ static int queue_bulk_sg_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		/* Chain all the TRBs together; clear the chain bit in the last
 		 * TRB to indicate it's the last TRB in the chain.
 		 */
-		if (num_trbs > 1) {
+		if (num_trbs > last_trb_num) {
 			field |= TRB_CHAIN;
-		} else {
-			/* FIXME - add check for ZERO_PACKET flag before this */
+		} else if (num_trbs == last_trb_num) {
 			td->last_trb = ep_ring->enqueue;
 			field |= TRB_IOC;
+		} else if (zero_length_needed && num_trbs == 1) {
+			trb_buff_len = 0;
+			urb_priv->td[1]->last_trb = ep_ring->enqueue;
+			field |= TRB_IOC;
 		}
 
 		/* Only set interrupt on short packet for IN endpoints */
@@ -3305,7 +3325,7 @@ static int queue_bulk_sg_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		if (running_total + trb_buff_len > urb->transfer_buffer_length)
 			trb_buff_len =
 				urb->transfer_buffer_length - running_total;
-	} while (running_total < urb->transfer_buffer_length);
+	} while (num_trbs > 0);
 
 	check_trb_math(urb, num_trbs, running_total);
 	giveback_first_trb(xhci, slot_id, ep_index, urb->stream_id,
@@ -3323,7 +3343,9 @@ int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 	int num_trbs;
 	struct xhci_generic_trb *start_trb;
 	bool first_trb;
+	int last_trb_num;
 	bool more_trbs_coming;
+	bool zero_length_needed;
 	int start_cycle;
 	u32 field, length_field;
 
@@ -3354,7 +3376,6 @@ int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		num_trbs++;
 		running_total += TRB_MAX_BUFF_SIZE;
 	}
-	/* FIXME: this doesn't deal with URB_ZERO_PACKET - need one more */
 
 	ret = prepare_transfer(xhci, xhci->devs[slot_id],
 			ep_index, urb->stream_id,
@@ -3363,6 +3384,20 @@ int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		return ret;
 
 	urb_priv = urb->hcpriv;
+
+	/* Deal with URB_ZERO_PACKET - need one more td/trb */
+	zero_length_needed = urb->transfer_flags & URB_ZERO_PACKET &&
+		urb_priv->length == 2;
+	if (zero_length_needed) {
+		num_trbs++;
+		xhci_dbg(xhci, "Creating zero length td.\n");
+		ret = prepare_transfer(xhci, xhci->devs[slot_id],
+				ep_index, urb->stream_id,
+				1, urb, 1, mem_flags);
+		if (ret < 0)
+			return ret;
+	}
+
 	td = urb_priv->td[0];
 
 	/*
@@ -3384,7 +3419,7 @@ int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		trb_buff_len = urb->transfer_buffer_length;
 
 	first_trb = true;
-
+	last_trb_num = zero_length_needed ? 2 : 1;
 	/* Queue the first TRB, even if it's zero-length */
 	do {
 		u32 remainder = 0;
@@ -3401,12 +3436,15 @@ int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		/* Chain all the TRBs together; clear the chain bit in the last
 		 * TRB to indicate it's the last TRB in the chain.
 		 */
-		if (num_trbs > 1) {
+		if (num_trbs > last_trb_num) {
 			field |= TRB_CHAIN;
-		} else {
-			/* FIXME - add check for ZERO_PACKET flag before this */
+		} else if (num_trbs == last_trb_num) {
 			td->last_trb = ep_ring->enqueue;
 			field |= TRB_IOC;
+		} else if (zero_length_needed && num_trbs == 1) {
+			trb_buff_len = 0;
+			urb_priv->td[1]->last_trb = ep_ring->enqueue;
+			field |= TRB_IOC;
 		}
 
 		/* Only set interrupt on short packet for IN endpoints */
@@ -3444,7 +3482,7 @@ int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 		trb_buff_len = urb->transfer_buffer_length - running_total;
 		if (trb_buff_len > TRB_MAX_BUFF_SIZE)
 			trb_buff_len = TRB_MAX_BUFF_SIZE;
-	} while (running_total < urb->transfer_buffer_length);
+	} while (num_trbs > 0);
 
 	check_trb_math(urb, num_trbs, running_total);
 	giveback_first_trb(xhci, slot_id, ep_index, urb->stream_id,
@@ -3511,8 +3549,8 @@ int xhci_queue_ctrl_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
 	if (start_cycle == 0)
 		field |= 0x1;
 
-	/* xHCI 1.0 6.4.1.2.1: Transfer Type field */
-	if (xhci->hci_version == 0x100) {
+	/* xHCI 1.0/1.1 6.4.1.2.1: Transfer Type field */
+	if (xhci->hci_version >= 0x100) {
 		if (urb->transfer_buffer_length > 0) {
 			if (setup->bRequestType & USB_DIR_IN)
 				field |= TRB_TX_TYPE(TRB_DATA_IN);
