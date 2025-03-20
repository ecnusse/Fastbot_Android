/*
 * This code is licensed under the Fastbot license. You may obtain a copy of this license in the LICENSE.txt file in the root directory of this source tree.
 */

package com.android.commands.monkey.tree;

import android.graphics.Rect;
import android.util.Xml;
import android.view.accessibility.AccessibilityNodeInfo;

import com.android.commands.monkey.utils.Logger;

import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.xmlpull.v1.XmlSerializer;

import java.io.IOException;
import java.io.StringWriter;

/**
 * @author Jianqiang Guo, Zhao Zhang
 */

/**
 * guitree builder utils
 * AccessibilityNodeInfo object -> xml string
 */
public class TreeBuilder {

    // copy from AccessibilityNodeInfoHelper
    static Rect getVisibleBoundsInScreen(AccessibilityNodeInfo node) {
        if (node == null) {
            return null;
        }
        // targeted node's bounds
        Rect nodeRect = new Rect();
        node.getBoundsInScreen(nodeRect);

        return nodeRect;
    }

    // copy from AccessibilityNodeInfoDumper
    private static String safeCharSeqToString(CharSequence cs) {
        if (cs == null)
            return "";
        else {
            return stripInvalidXMLChars(cs);
        }
    }

    // copy from AccessibilityNodeInfoDumper
    private static String stripInvalidXMLChars(CharSequence cs) {
        StringBuffer ret = new StringBuffer();
        char ch;
        /* http://www.w3.org/TR/xml11/#charsets
        [#x1-#x8], [#xB-#xC], [#xE-#x1F], [#x7F-#x84], [#x86-#x9F], [#xFDD0-#xFDDF],
        [#x1FFFE-#x1FFFF], [#x2FFFE-#x2FFFF], [#x3FFFE-#x3FFFF],
        [#x4FFFE-#x4FFFF], [#x5FFFE-#x5FFFF], [#x6FFFE-#x6FFFF],
        [#x7FFFE-#x7FFFF], [#x8FFFE-#x8FFFF], [#x9FFFE-#x9FFFF],
        [#xAFFFE-#xAFFFF], [#xBFFFE-#xBFFFF], [#xCFFFE-#xCFFFF],
        [#xDFFFE-#xDFFFF], [#xEFFFE-#xEFFFF], [#xFFFFE-#xFFFFF],
        [#x10FFFE-#x10FFFF].
         */
        try {
            for (int i = 0; i < cs.length(); i++) {
                ch = cs.charAt(i);

                if ((ch >= 0x1 && ch <= 0x8) || (ch >= 0xB && ch <= 0xC) || (ch >= 0xE && ch <= 0x1F) ||
                        (ch >= 0x7F && ch <= 0x84) || (ch >= 0x86 && ch <= 0x9f) ||
                        (ch >= 0xFDD0 && ch <= 0xFDDF) || (ch >= 0x1FFFE && ch <= 0x1FFFF) ||
                        (ch >= 0x2FFFE && ch <= 0x2FFFF) || (ch >= 0x3FFFE && ch <= 0x3FFFF) ||
                        (ch >= 0x4FFFE && ch <= 0x4FFFF) || (ch >= 0x5FFFE && ch <= 0x5FFFF) ||
                        (ch >= 0x6FFFE && ch <= 0x6FFFF) || (ch >= 0x7FFFE && ch <= 0x7FFFF) ||
                        (ch >= 0x8FFFE && ch <= 0x8FFFF) || (ch >= 0x9FFFE && ch <= 0x9FFFF) ||
                        (ch >= 0xAFFFE && ch <= 0xAFFFF) || (ch >= 0xBFFFE && ch <= 0xBFFFF) ||
                        (ch >= 0xCFFFE && ch <= 0xCFFFF) || (ch >= 0xDFFFE && ch <= 0xDFFFF) ||
                        (ch >= 0xEFFFE && ch <= 0xEFFFF) || (ch >= 0xFFFFE && ch <= 0xFFFFF) ||
                        (ch >= 0x10FFFE && ch <= 0x10FFFF))
                    ret.append(".");
                else
                    ret.append(ch);
            }
        } catch (Exception ex) {
            Logger.println("strip invalid xml failed!, repace all &# " + ex.toString());
            return cs.toString().replaceAll("\"", "").replaceAll("&#", "");
        }

        return ret.toString();
    }

    // copy from AccessibilityNodeInfoDumper
    private static void dumpNodeRec(AccessibilityNodeInfo node, XmlSerializer serializer, int index,
                                    int depth)  throws IOException {
        serializer.startTag("", "node");
        // do not need naf check for document xml
        //if (!nafExcludedClass(node) && !nafCheck(node))
        //serializer.attribute("", "NAF", Boolean.toString(true));
        serializer.attribute("", "index", Integer.toString(index));
        serializer.attribute("", "text", safeCharSeqToString(node.getText()));
        serializer.attribute("", "resource-id", safeCharSeqToString(node.getViewIdResourceName()));
        serializer.attribute("", "class", safeCharSeqToString(node.getClassName()));
        serializer.attribute("", "package", safeCharSeqToString(node.getPackageName()));
        serializer.attribute("", "content-desc", safeCharSeqToString(node.getContentDescription()));
        serializer.attribute("", "checkable", Boolean.toString(node.isCheckable()));
        serializer.attribute("", "checked", Boolean.toString(node.isChecked()));
        serializer.attribute("", "clickable", Boolean.toString(node.isClickable()));
        serializer.attribute("", "enabled", Boolean.toString(node.isEnabled()));
        serializer.attribute("", "focusable", Boolean.toString(node.isFocusable()));
        serializer.attribute("", "focused", Boolean.toString(node.isFocused()));
        serializer.attribute("", "scrollable", Boolean.toString(node.isScrollable()));
        serializer.attribute("", "long-clickable", Boolean.toString(node.isLongClickable()));
        serializer.attribute("", "password", Boolean.toString(node.isPassword()));
        serializer.attribute("", "selected", Boolean.toString(node.isSelected()));
        serializer.attribute("", "bounds", getVisibleBoundsInScreen(node).toShortString());

        depth += 1;
        if(depth <= 25) {
            int count = node.getChildCount();
            for (int i = 0; i < count; i++) {
                AccessibilityNodeInfo child = node.getChild(i);
                if (child != null && child.isVisibleToUser()) {
                    dumpNodeRec(child, serializer, i, depth);
                    child.recycle();
                }
            }
            serializer.endTag("", "node");
        }
    }

    private static void dumpNodeRec(Element node, XmlSerializer serializer, int index, int depth) throws IOException {
        // 开始节点标签
        serializer.startTag("", "node");

        serializer.attribute("", "index", node.hasAttribute("index") ? node.getAttribute("index") : String.valueOf(index));
        serializer.attribute("", "text", node.getAttribute("text"));
        serializer.attribute("", "resource-id", node.getAttribute("resource-id"));
        serializer.attribute("", "class", node.getAttribute("class"));
        serializer.attribute("", "package", node.getAttribute("package"));
        serializer.attribute("", "content-desc", node.getAttribute("content-desc"));
        serializer.attribute("", "checkable", node.getAttribute("checkable"));
        serializer.attribute("", "checked", node.getAttribute("checked"));
        serializer.attribute("", "clickable", node.getAttribute("clickable"));
        serializer.attribute("", "enabled", node.getAttribute("enabled"));
        serializer.attribute("", "focusable", node.getAttribute("focusable"));
        serializer.attribute("", "focused", node.getAttribute("focused"));
        serializer.attribute("", "scrollable", node.getAttribute("scrollable"));
        serializer.attribute("", "long-clickable", node.getAttribute("long-clickable"));
        serializer.attribute("", "password", node.getAttribute("password"));
        serializer.attribute("", "selected", node.getAttribute("selected"));
        serializer.attribute("", "bounds", node.getAttribute("bounds"));


        depth += 1;

        if (depth <= 25) {
            // 处理子 Element 节点
            NodeList childNodes = node.getChildNodes();
            int childElementIndex = 0;
            boolean hasChildElement = false;
            for (int i = 0; i < childNodes.getLength(); i++) {
                Node child = childNodes.item(i);
                if (child.getNodeType() == Node.ELEMENT_NODE) {
                    hasChildElement = true;
                    dumpNodeRec((Element) child, serializer, childElementIndex, depth);
                    childElementIndex++;
                }
            }
            serializer.endTag("", "node");
        }
    }

    /**
     * Using {@link AccessibilityNodeInfo} this method will walk the layout hierarchy
     * and generates an xml dump to the location specified by <code>dumpFile</code>
     */
    public static String dumpDocumentStrWithOutTree(AccessibilityNodeInfo rootInfo) {
        String dumpstrRet = "";
        try {
            StringWriter textWriter = new StringWriter();
            XmlSerializer serializer = Xml.newSerializer();
            serializer.setOutput(textWriter);
            serializer.startDocument("UTF-8", true);
            serializer.setFeature("http://xmlpull.org/v1/doc/features.html#indent-output", true);

            int depth = 1;
            dumpNodeRec(rootInfo, serializer, 0, depth);
            serializer.endDocument();
            dumpstrRet = textWriter.toString();
        } catch (IllegalArgumentException | IOException | IllegalStateException e) {
            Logger.println("failed to dump window to file: " + e.toString());
        }
        return dumpstrRet;
    }

    /**
     * Filter the attributes in U2 XML tree
     * @param root
     */
    public static void filterTree(Element root) {
        if (root == null) {
            return;
        }

        // filter the necessary attrs
        String[] attributeToRemove = {"drawing-order", "hint", "display-id"};
        for (String attr : attributeToRemove) {
            if (root.hasAttribute(attr)) {
                root.removeAttribute(attr);
            }
        }

        NodeList childNodes = root.getChildNodes();
        for (int i = 0; i < childNodes.getLength(); i++) {
            Node node = childNodes.item(i);
            if (node.getNodeType() == Node.ELEMENT_NODE) {
                Element child = (Element) node;
                filterTree(child);
            }
        }
    }

    /**
     * Using {@link AccessibilityNodeInfo} this method will walk the layout hierarchy
     * and generates an xml dump to the location specified by <code>dumpFile</code>
     */
    public static String dumpDocumentStrWithOutTree(Element rootInfo) {
        String dumpstrRet = "";
        try {
            StringWriter textWriter = new StringWriter();
            XmlSerializer serializer = Xml.newSerializer();
            serializer.setOutput(textWriter);
            serializer.startDocument("UTF-8", true);
            serializer.setFeature("http://xmlpull.org/v1/doc/features.html#indent-output", true);

            int depth = 1;
            dumpNodeRec(rootInfo, serializer, 0, depth);
            serializer.endDocument();
            dumpstrRet = textWriter.toString();
        } catch (IllegalArgumentException | IOException | IllegalStateException e) {
            Logger.println("failed to dump window to file: " + e.toString());
        }
        return dumpstrRet;
    }
}
