package com.android.commands.monkey.events;
import org.w3c.dom.Element;

import com.android.commands.monkey.events.MonkeyEvent;

/**
 * Event source interface
 */
public interface MonkeyEventSourceU2 extends MonkeyEventSource {
    /**
     * 获取并移除队列中的第一个事件
     *
     * @return 下一个 Monkey 事件
     */
    MonkeyEvent getNextEvent();

    /**
     * 设置日志输出的详细程度
     *
     * @param verbose 日志模式，1 表示详细，2 表示非常详细
     */
    void setVerbose(int verbose);

    /**
     * 验证事件源是否有效
     *
     * @return 如果验证失败返回 false，例如随机源中的因子失败或脚本源中的文件无法打开等
     */
    boolean validate();

    /**
     * 获取活动窗口的根元素
     *
     * @return 活动窗口的根元素
     */
    Element getRootInActiveWindow();

    /**
     * 慢速获取活动窗口的根元素
     *
     * @return 活动窗口的根元素
     */
    Element getRootInActiveWindowSlow();

    /**
     * 处理系统 UI
     *
     * @param info UI 元素信息
     * @return 如果处理了系统 UI 返回 true，否则返回 false
     */
    boolean dealWithSystemUI(Element info);

    void updateActivityHistory();
}