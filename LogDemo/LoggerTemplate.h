#pragma once
#include <QString>
#include <string>
namespace Logger
{
    const static QString logTemplate = u8R"logTemplate(
<?xml version="1.0" encoding="utf-8" standalone="yes"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">
<html>

<head>
    <title>TaoLogger</title>
    <meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<style type="text/css" id="logCss">
    body {
        background: #1e1e1e;
        color: #d4d4d4;
        margin: 20px;
        font-size: 14px;
        font-family: 'Consolas', monospace;
    }

    .d, .w, .c, .f, .i {
        padding: 8px;
        margin: 4px 0;
        border-radius: 4px;
        font-family: 'Consolas', monospace;
    }

    /* Debug - 灰色 */
    .d {
        background-color: #2d2d2d;
        color: #a0a0a0;
        border-left: 4px solid #a0a0a0;
    }

    /* Info - 蓝色 */
    .i {
        background-color: #2b3d4f;
        color: #61afef;
        border-left: 4px solid #61afef;
    }

    /* Warning - 黄色 */
    .w {
        background-color: #4d4d2d;
        color: #e5c07b;
        border-left: 4px solid #e5c07b;
    }

    /* Critical - 橙色 */
    .c {
        background-color: #4d3427;
        color: #ff9f43;
        border-left: 4px solid #ff9f43;
    }

    /* Fatal - 红色 */
    .f {
        background-color: #4d2c2c;
        color: #ff6b6b;
        border-left: 4px solid #ff6b6b;
    }

    /* 下拉框样式 */
    #typeSelect {
        padding: 8px;
        margin: 10px 0;
        background: #2d2d2d;
        color: #d4d4d4;
        border: 1px solid #404040;
        border-radius: 4px;
        font-family: 'Consolas', monospace;
    }

    h1 {
        color: #d4d4d4;
        text-align: center;
        padding: 20px 0;
    }
</style>
</head>

<body>
    <h1><a href="https://jaredtao.github.io">TaoLogger</a> 日志文件</h1>
    <script type="text/JavaScript">
        function objHide(obj) {
            obj.style.display="none"
        }
        function objShow(obj) {
            obj.style.display="block"
        }
        function selectType() {
            var sel = document.getElementById("typeSelect");
            const hideList = new Set(['d', 'i', 'w', 'c', 'f']);
            if (sel.value === 'a') {
                hideList.forEach(element => {
                    var list = document.querySelectorAll('.' + element);
                    list.forEach(objShow);
                });
            } else {
                var ss = hideList;
                ss.delete(sel.value);
                ss.forEach(element => {
                    var list = document.querySelectorAll('.' + element);
                    list.forEach(objHide);
                });
                var showList = document.querySelectorAll('.' + sel.value);
                showList.forEach(objShow);
            }
        }
    </script>
    <select id="typeSelect" onchange="selectType()">
        <option value='a' selected="selected">All</option>
        <option value='d'>Debug</option>
        <option value='i'>Info</option>
        <option value='w'>Warning</option>
        <option value='c'>Critical</option>
        <option value='f'>Fatal</option>
    </select>
)logTemplate";
}
