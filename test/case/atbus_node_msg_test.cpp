﻿#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iomanip>

#include "common/string_oprs.h"

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

#include <atbus_node.h>

#include "detail/libatbus_protocol.h"

#include "frame/test_macros.h"

#include <stdarg.h>

static void node_msg_test_on_debug(const char* file_path, size_t line, 
    const atbus::node& n, const atbus::endpoint* ep, const atbus::connection* conn, 
    const atbus::protocol::msg* m,
    const char* fmt, ...) {
    size_t offset = 0;
    for (size_t i = 0; file_path[i]; ++i) {
        if ('/' == file_path[i] || '\\' == file_path[i]) {
            offset = i + 1;
        }
    }
    file_path += offset;

    std::streamsize w = std::cout.width();
    CASE_MSG_INFO() << "[Log Debug][" << std::setw(24) << file_path << ":" << std::setw(4) << line << "] node=0x" << 
        std::setfill('0')<< std::hex<< std::setw(8)<< n.get_id() <<
        ", endpoint=0x" << std::setw(8)<< (NULL == ep ? 0 : ep->get_id()) <<
        ", connection=" << conn<< std::setfill(' ')<< std::setw(w)<<  std::dec<<
        "\t";

    va_list ap;
    va_start(ap, fmt);

    vprintf(fmt, ap);

    va_end(ap);

    puts("");
}

struct node_msg_test_recv_msg_record_t {
    const atbus::node* n;
    const atbus::endpoint* ep;
    const atbus::connection* conn;
    std::string data;
    int status;
    int count;

    node_msg_test_recv_msg_record_t(): n(NULL), ep(NULL), conn(NULL), status(0), count(0) {}
};

static node_msg_test_recv_msg_record_t recv_msg_history;

static int node_msg_test_recv_msg_test_record_fn(const atbus::node& n, const atbus::endpoint* ep, const atbus::connection* conn, 
    int status, const void* buffer, size_t len) {
    recv_msg_history.n = &n;
    recv_msg_history.ep = ep;
    recv_msg_history.conn = conn;
    recv_msg_history.status = status;
    ++recv_msg_history.count;

    if (NULL != buffer && len > 0) {
        recv_msg_history.data.assign(reinterpret_cast<const char*>(buffer), len);
    } else {
        recv_msg_history.data.clear();
    }

    return 0;
}

static int node_msg_test_send_data_failed_fn(const atbus::node& n, const atbus::endpoint* ep, const atbus::connection* conn, 
    const atbus::protocol::msg* m) {
    recv_msg_history.n = &n;
    recv_msg_history.ep = ep;
    recv_msg_history.conn = conn;
    recv_msg_history.status = NULL == m? 0: m->head.ret;
    ++recv_msg_history.count;

    if (NULL != m && NULL != m->body.forward && NULL != m->body.forward->content.ptr && m->body.forward->content.size > 0) {
        recv_msg_history.data.assign(reinterpret_cast<const char*>(m->body.forward->content.ptr), m->body.forward->content.size);
    } else {
        recv_msg_history.data.clear();
    }

    return 0;
}

static void node_msg_test_setup_exit(uv_loop_t* ev) {
    size_t left_tick = 128 * 30; // 30s
    while (left_tick > 0 && UV_EBUSY == uv_loop_close(ev)) {
        uv_run(ev, UV_RUN_NOWAIT);
        CASE_THREAD_SLEEP_MS(8);
        
        -- left_tick;
    }
    
    CASE_EXPECT_NE(left_tick, 0);
}

// 定时Ping Pong协议测试
CASE_TEST(atbus_node_reg, ping_pong)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug = node_msg_test_on_debug;
        node2->on_debug = node_msg_test_on_debug;

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL) + 1;
        node1->proc(proc_t, 0);
        node2->proc(proc_t, 0);
        node1->connect("ipv4://127.0.0.1:16388");

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);

            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }
        }
        
        proc_t += conf.ping_interval;
        while(true) {
            ++ proc_t;
            node1->proc(proc_t, 0);
            node2->proc(proc_t, 0);
            
            uv_run(&ev_loop, UV_RUN_NOWAIT);
            
            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());
            
            if (NULL == ep1 || NULL == ep2 || NULL == ep1->get_data_connection(ep2) || NULL == ep2->get_data_connection(ep1)) {
                CASE_EXPECT_NE(NULL, ep1);
                CASE_EXPECT_NE(NULL, ep2);
                CASE_EXPECT_NE(NULL, ep1->get_data_connection(ep2));
                CASE_EXPECT_NE(NULL, ep2->get_data_connection(ep1));
                break;
            }
            
            if (ep1->get_stat_last_pong() > 0 && ep2->get_stat_last_pong() > 0) {
                break;
            }
        }
        
    }

    node_msg_test_setup_exit(&ev_loop);
}

static int node_msg_test_recv_msg_test_custom_cmd_fn(const atbus::node&, const atbus::endpoint*, const atbus::connection*, atbus::node::bus_id_t, const std::vector<std::pair<const void*, size_t> >& data) {
    ++recv_msg_history.count;
    
    recv_msg_history.data.clear();
    for(size_t i = 0; i < data.size(); ++ i) {
        recv_msg_history.data.append(static_cast<const char*>(data[i].first), data[i].second);
        recv_msg_history.data += '\0';
    }
    
    return 0;
}

// 自定义命令协议测试
CASE_TEST(atbus_node_reg, custom_cmd)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    do {
        atbus::node::ptr_t node1 = atbus::node::create();
        atbus::node::ptr_t node2 = atbus::node::create();
        node1->on_debug = node_msg_test_on_debug;
        node2->on_debug = node_msg_test_on_debug;

        node1->init(0x12345678, &conf);
        node2->init(0x12356789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node2->start());

        time_t proc_t = time(NULL) + 1;
        node1->proc(proc_t, 0);
        node2->proc(proc_t, 0);
        node1->connect("ipv4://127.0.0.1:16388");

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);

            atbus::endpoint* ep1 = node2->get_endpoint(node1->get_id());
            atbus::endpoint* ep2 = node1->get_endpoint(node2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }
        }

        int count = recv_msg_history.count;
        node2->set_on_custom_cmd_handle(node_msg_test_recv_msg_test_custom_cmd_fn);
        
        char test_str[] = "hello world!";
        std::string send_data = test_str;
        const void* custom_data[3];
        custom_data[0] = &test_str[0];
        custom_data[1] = &test_str[6];
        custom_data[2] = &test_str[11];
        size_t custom_len[] = { 5, 5, 1 };
        
        send_data[5] = '\0';
        send_data[11] = '\0';
        send_data += '!';
        send_data += '\0';
        
        CASE_EXPECT_EQ(0, node1->send_custom_cmd(node2->get_id(), custom_data, custom_len, 3));

        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);
            if (count != recv_msg_history.count) {
                break;
            }
        }

        CASE_EXPECT_EQ(send_data, recv_msg_history.data);
    } while(false);

    node_msg_test_setup_exit(&ev_loop);
}

// 发给自己
CASE_TEST(atbus_node_reg, reset_and_send)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node1 = atbus::node::create();
        node1->on_debug = node_msg_test_on_debug;

        node1->init(0x12345678, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->listen("ipv4://127.0.0.1:16387"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node1->start());

        time_t proc_t = time(NULL) + 1;
        node1->proc(proc_t, 0);

        std::string send_data;
        send_data.assign("self\0hello world!\n", sizeof("self\0hello world!\n") - 1);

        int count = recv_msg_history.count;
        node1->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        node1->send_data(node1->get_id(), 0, send_data.data(), send_data.size());

        CASE_EXPECT_EQ(count + 1, recv_msg_history.count)
        CASE_EXPECT_EQ(send_data, recv_msg_history.data);
    }

    node_msg_test_setup_exit(&ev_loop);
}

// 父子节点消息转发测试
CASE_TEST(atbus_node_reg, parent_and_child)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;

    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child = atbus::node::create();
        node_parent->on_debug = node_msg_test_on_debug;
        node_child->on_debug = node_msg_test_on_debug;

        node_parent->init(0x12345678, &conf);
        
        conf.children_mask = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child->init(0x12346789, &conf);

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child->start());

        time_t proc_t = time(NULL) + 1;
        // 注册成功自动会有可用的端点
        for (int i = 0; i < 512; ++i) {
            node_parent->proc(proc_t, 0);
            node_child->proc(proc_t, 0);

            atbus::endpoint* ep1 = node_child->get_endpoint(node_parent->get_id());
            atbus::endpoint* ep2 = node_parent->get_endpoint(node_child->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }

            uv_run(conf.ev_loop, UV_RUN_ONCE);
            
            ++ proc_t;
        }
        
        node_child->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        node_parent->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        
        int count = recv_msg_history.count;
        
        // 发消息啦 -  parent to child
        {
            std::string send_data;
            send_data.assign("parent to child\0hello world!\n", sizeof("parent to child\0hello world!\n") - 1);
            
            node_parent->send_data(node_child->get_id(), 0, send_data.data(), send_data.size());
            for (int i = 0; i < 256; ++i) {
                uv_run(conf.ev_loop, UV_RUN_ONCE);
                CASE_THREAD_SLEEP_MS(16);
                if (count != recv_msg_history.count) {
                    break;
                }
            }

            CASE_EXPECT_EQ(send_data, recv_msg_history.data);
        }
        
        // 发消息啦 - child to parent
        {
            std::string send_data;
            send_data.assign("child to parent\0hello world!\n", sizeof("child to parent\0hello world!\n") - 1);
            
            count = recv_msg_history.count;
            node_child->send_data(node_parent->get_id(), 0, send_data.data(), send_data.size());
            for (int i = 0; i < 256; ++i) {
                uv_run(conf.ev_loop, UV_RUN_ONCE);
                CASE_THREAD_SLEEP_MS(16);
                if (count != recv_msg_history.count) {
                    break;
                }
            }

            CASE_EXPECT_EQ(send_data, recv_msg_history.data);
        }
    }

    node_msg_test_setup_exit(&ev_loop);
}

// 兄弟节点通过父节点转发消息并建立直连测试（测试路由）
CASE_TEST(atbus_node_reg, transfer_and_connect)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;
    
    // 只有发生冲突才会注册不成功，否则会无限重试注册父节点，直到其上线
    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child_1 = atbus::node::create();
        atbus::node::ptr_t node_child_2 = atbus::node::create();
        node_parent->on_debug = node_msg_test_on_debug;
        node_child_1->on_debug = node_msg_test_on_debug;
        node_child_2->on_debug = node_msg_test_on_debug;

        node_parent->init(0x12345678, &conf);
        
        conf.children_mask = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child_1->init(0x12346789, &conf);
        node_child_2->init(0x12346890, &conf);
        
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->listen("ipv4://127.0.0.1:16388"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_2->listen("ipv4://127.0.0.1:16389"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_2->start());

        time_t proc_t = time(NULL) + 1;
        node_child_1->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        node_child_2->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        
        // wait for register finished
        for (int i = 0; i < 256; ++i) {
            node_parent->proc(proc_t, 0);
            node_child_1->proc(proc_t, 0);
            node_child_2->proc(proc_t, 0);

            atbus::endpoint* ep1 = node_child_1->get_endpoint(node_parent->get_id());
            atbus::endpoint* ep2 = node_parent->get_endpoint(node_child_1->get_id());
            atbus::endpoint* ep3 = node_parent->get_endpoint(node_child_2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep3 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep3)) {
                break;
            }
            
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            ++ proc_t;
        }
        
        // 转发消息
        std::string send_data;
        send_data.assign("transfer through parent\n", sizeof("transfer through parent\n") - 1);
        
        int count = recv_msg_history.count;
        node_child_1->send_data(node_child_2->get_id(), 0, send_data.data(), send_data.size());
        for (int i = 0; i < 256; ++i) {
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            CASE_THREAD_SLEEP_MS(16);
            if (count != recv_msg_history.count) {
                break;
            }
        }

        CASE_EXPECT_EQ(send_data, recv_msg_history.data);
        
        // 自动直连测试
        for (int i = 0; i < 512 && NULL == node_child_1->get_endpoint(node_child_2->get_id()); ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(8);
        }
        atbus::endpoint* ep1 = node_child_1->get_endpoint(node_child_2->get_id());
        CASE_EXPECT_NE(NULL, ep1);
    }

    node_msg_test_setup_exit(&ev_loop);
}

// 兄弟节点通过多层父节点转发消息并不会建立直连测试
CASE_TEST(atbus_node_reg, transfer_only)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;
    
    // 只有发生冲突才会注册不成功，否则会无限重试注册父节点，直到其上线
    {
        atbus::node::ptr_t node_parent_1 = atbus::node::create();
        atbus::node::ptr_t node_parent_2 = atbus::node::create();
        atbus::node::ptr_t node_child_1 = atbus::node::create();
        atbus::node::ptr_t node_child_2 = atbus::node::create();
        node_parent_1->on_debug = node_msg_test_on_debug;
        node_parent_2->on_debug = node_msg_test_on_debug;
        node_child_1->on_debug = node_msg_test_on_debug;
        node_child_2->on_debug = node_msg_test_on_debug;

        node_parent_1->init(0x12345678, &conf);
        node_parent_2->init(0x12356789, &conf);
        
        conf.children_mask = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child_1->init(0x12346789, &conf);
        conf.father_address = "ipv4://127.0.0.1:16388";
        node_child_2->init(0x12354678, &conf);
        
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent_1->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent_2->listen("ipv4://127.0.0.1:16388"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->listen("ipv4://127.0.0.1:16389"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_2->listen("ipv4://127.0.0.1:16390"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent_1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent_2->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_2->start());

        time_t proc_t = time(NULL) + 1;
        node_child_1->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        node_child_2->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        node_parent_1->connect("ipv4://127.0.0.1:16388");
        
        // wait for register finished
        for (int i = 0; i < 512; ++i) {
            node_parent_1->proc(proc_t, 0);
            node_parent_2->proc(proc_t, 0);
            node_child_1->proc(proc_t, 0);
            node_child_2->proc(proc_t, 0);

            atbus::endpoint* ep1 = node_child_1->get_endpoint(node_parent_1->get_id());
            atbus::endpoint* ep2 = node_parent_1->get_endpoint(node_child_1->get_id());
            atbus::endpoint* ep3 = node_child_2->get_endpoint(node_parent_2->get_id());
            atbus::endpoint* ep4 = node_parent_2->get_endpoint(node_child_2->get_id());
            atbus::endpoint* ep5 = node_parent_1->get_endpoint(node_parent_2->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep3 && NULL != ep4 && NULL != ep5 && 
                NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1) &&
                NULL != ep3->get_data_connection(ep4) && NULL != ep4->get_data_connection(ep3) &&
                NULL != ep5->get_data_connection(ep3)) {
                break;
            }
            
            uv_run(conf.ev_loop, UV_RUN_ONCE);
            ++ proc_t;
        }
        
        // 转发消息
        std::string send_data;
        send_data.assign("transfer through parent only\n", sizeof("transfer through parent only\n") - 1);
        
        int count = recv_msg_history.count;
        node_child_1->send_data(node_child_2->get_id(), 0, send_data.data(), send_data.size());
        for (int i = 0; i < 512; ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(8);
            if (count != recv_msg_history.count) {
                break;
            }
        }

        CASE_EXPECT_EQ(send_data, recv_msg_history.data);
        for (int i = 0; i < 128; ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(4);
        }
        
        // 非直接子节点互相不建立直连
        atbus::endpoint* ep1 = node_child_1->get_endpoint(node_child_2->get_id());
        CASE_EXPECT_EQ(NULL, ep1);
    }

    node_msg_test_setup_exit(&ev_loop);
}

// 直连节点发送失败测试
CASE_TEST(atbus_node_reg, send_failed)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;
    
    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        node_parent->on_debug = node_msg_test_on_debug;
        node_parent->init(0x12345678, &conf);
        
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        
        std::string send_data;
        send_data.assign("send failed", sizeof("send failed") - 1);
        
        // send to child failed
        CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_INVALID_ID, node_parent->send_data(0x12346780, 0, send_data.data(), send_data.size()));
        // send to brother and failed
        CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_INVALID_ID, node_parent->send_data(0x12356789, 0, send_data.data(), send_data.size()));
    }
    
    node_msg_test_setup_exit(&ev_loop);
}

// 发送给子节点转发失败的回复通知测试
// 发送给父节点转发失败的回复通知测试
CASE_TEST(atbus_node_reg, transfer_failed)
{
    atbus::node::conf_t conf;
    atbus::node::default_conf(&conf);
    conf.children_mask = 16;
    uv_loop_t ev_loop;
    uv_loop_init(&ev_loop);

    conf.ev_loop = &ev_loop;
    
    // 只有发生冲突才会注册不成功，否则会无限重试注册父节点，直到其上线
    {
        atbus::node::ptr_t node_parent = atbus::node::create();
        atbus::node::ptr_t node_child_1 = atbus::node::create();
        node_parent->on_debug = node_msg_test_on_debug;
        node_child_1->on_debug = node_msg_test_on_debug;

        node_parent->init(0x12345678, &conf);
        
        conf.children_mask = 8;
        conf.father_address = "ipv4://127.0.0.1:16387";
        node_child_1->init(0x12346789, &conf);
        
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->listen("ipv4://127.0.0.1:16387"));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->listen("ipv4://127.0.0.1:16388"));

        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_parent->start());
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->start());

        time_t proc_t = time(NULL) + 1;
        node_child_1->set_on_recv_handle(node_msg_test_recv_msg_test_record_fn);
        node_child_1->set_on_send_data_failed_handle(node_msg_test_send_data_failed_fn);
        
        // wait for register finished
        for (int i = 0; i < 256; ++i) {
            node_parent->proc(proc_t, 0);
            node_child_1->proc(proc_t, 0);

            atbus::endpoint* ep1 = node_child_1->get_endpoint(node_parent->get_id());
            atbus::endpoint* ep2 = node_parent->get_endpoint(node_child_1->get_id());

            if (NULL != ep1 && NULL != ep2 && NULL != ep1->get_data_connection(ep2) && NULL != ep2->get_data_connection(ep1)) {
                break;
            }

            uv_run(conf.ev_loop, UV_RUN_ONCE);
            
            ++ proc_t;
        }
        
        // 转发消息
        std::string send_data;
        send_data.assign("transfer through parent\n", sizeof("transfer through parent\n") - 1);
        
        int count = recv_msg_history.count;
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->send_data(0x12346890, 0, send_data.data(), send_data.size()));
        CASE_EXPECT_EQ(EN_ATBUS_ERR_SUCCESS, node_child_1->send_data(0x12356789, 0, send_data.data(), send_data.size()));
        for (int i = 0; i < 512; ++i) {
            uv_run(conf.ev_loop, UV_RUN_NOWAIT);
            CASE_THREAD_SLEEP_MS(8);
            if (count + 1 < recv_msg_history.count) {
                break;
            }
        }

        CASE_EXPECT_EQ(count + 2, recv_msg_history.count);
        CASE_EXPECT_EQ(EN_ATBUS_ERR_ATNODE_INVALID_ID, recv_msg_history.status);
    }

    while (UV_EBUSY == uv_loop_close(&ev_loop)) {
        uv_run(&ev_loop, UV_RUN_ONCE);
    }
}

// TODO 发送给已下线兄弟节点并失败的回复通知测试（网络失败）


// TODO 全量表第一次拉取测试
// TODO 全量表通知给父节点和子节点测试
