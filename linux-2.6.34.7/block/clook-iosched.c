/*
 * elevator clook
 * CS411 Group 13
 * James Admire, Trey Trahin, Daniel Urbanski, & Lewis Valentine
 * Implemented CLOOK i/o scheduling
 *
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

struct clook_data {
	struct list_head queue;
	sector_t last_sector; /* Head Location  */
};

static void clook_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int clook_dispatch(struct request_queue *q, int force)
{
	struct clook_data *nd = q->elevator->elevator_data;

	if (!list_empty(&nd->queue)) {
		struct request *rq;
		rq = list_entry(nd->queue.next, struct request, queuelist);
		list_del_init(&rq->queuelist);
		elv_dispatch_sort(q, rq);
		nd->last_sector = blk_rq_pos(rq) + blk_rq_sectors(rq); /* update head location */
		printk("[CLOOK] dsp %s %llu head: %llu \n", rq_data_dir(rq) ? "W":"R", blk_rq_pos(rq), nd->last_sector);
		return 1;
	}
	return 0;
}

static void clook_add_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *nd = q->elevator->elevator_data;
	struct request *pos;
	if(list_empty(&nd->queue)){
		list_add(&rq->queuelist, &nd->queue);
		printk("[CLOOK] add %s %llu \n", rq_data_dir(rq) ? "W":"R", blk_rq_pos(rq));
		return;
	}
	list_for_each_entry(pos, &nd->queue, queuelist){ //might need to check last_pos val
		if(blk_rq_pos(pos) > blk_rq_pos(rq) && (blk_rq_pos(rq) > nd->last_sector || (blk_rq_pos(pos) < nd->last_sector)){
			list_add_tail(&rq->queuelist, &pos->queuelist);
			printk("[CLOOK] add %s %llu \n", rq_data_dir(rq) ? "W":"R", 
				blk_rq_pos(rq));
			return;
		}
	}
	list_add_tail(&rq->queuelist, &nd->queue);
	printk("[CLOOK] add %s %llu \n", rq_data_dir(rq) ? "W":"R", blk_rq_pos(rq));
}

static int clook_queue_empty(struct request_queue *q)
{
	struct clook_data *nd = q->elevator->elevator_data;
  
	return list_empty(&nd->queue);
}

static struct request *
clook_former_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
clook_latter_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *nd = q->elevator->elevator_data;

	if (rq->queuelist.next == &nd->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static void *clook_init_queue(struct request_queue *q)
{
	struct clook_data *nd;

	nd = kmalloc_node(sizeof(*nd), GFP_KERNEL, q->node);
	if (!nd)
		return NULL;
	INIT_LIST_HEAD(&nd->queue);
	nd->last_sector = 0;
	return nd;
}

static void clook_exit_queue(struct elevator_queue *e)
{
	struct clook_data *nd = e->elevator_data;

	BUG_ON(!list_empty(&nd->queue));
	kfree(nd);
}

static struct elevator_type elevator_clook = {
	.ops = {
		.elevator_merge_req_fn		= clook_merged_requests,
		.elevator_dispatch_fn		= clook_dispatch,
		.elevator_add_req_fn		= clook_add_request,
		.elevator_queue_empty_fn	= clook_queue_empty,
		.elevator_former_req_fn		= clook_former_request,
		.elevator_latter_req_fn		= clook_latter_request,
		.elevator_init_fn		= clook_init_queue,
		.elevator_exit_fn		= clook_exit_queue,
	},
	.elevator_name = "clook",
	.elevator_owner = THIS_MODULE,
};

static int __init clook_init(void)
{
	elv_register(&elevator_clook);

	return 0;
}

static void __exit clook_exit(void)
{
	elv_unregister(&elevator_clook);
}

module_init(clook_init);
module_exit(clook_exit);


MODULE_AUTHOR("Group13");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clook IO scheduler");
