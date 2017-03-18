#include "main.hpp"
#include "../VNodeData/VNodeData.hpp"
#include "../VNode/VNode.hpp"
#include "../HtmlDOMApi/HtmlDOMApi.hpp"
#include "../Utils/utils.hpp"
#include <algorithm>
#include <vector>
#include <map>
#include <string>

bool sameVnode(VNode vnode1, VNode vnode2) {
  return vnode1.key == vnode2.key && vnode1.sel == vnode2.sel;
};

VNode emptyNodeAt(emscripten::val elm) {
  VNode vnode = VNode();
  vnode.elm = elm;
  std::string id;
  if (isDefined(elm["id"])) {
    id += '#';
    id.append(elm["id"].as<std::string>());
  }
  std::string c;
  if (isDefined(elm["className"])) {
    c += '.';
    c.append(elm["className"].as<std::string>());
    std::replace(c.begin(), c.end(), ' ', '.');
  }
  vnode.sel.append(tagName(elm));
  std::transform(vnode.sel.begin(), vnode.sel.end(), vnode.sel.begin(), ::tolower);
  vnode.sel.append(id);
  vnode.sel.append(c);
  return vnode;
};

std::map<std::string, int> createKeyToOldIdx(std::vector<VNode> children, int beginIdx, int endIdx) {
  std::size_t i = beginIdx;
	std::map<std::string, int> map;
  for (; i <= endIdx; i++) {
    if (!children[i].key.empty()) {
			map[children[i].key] = i;
		}
  }
  return map;
}

emscripten::val createElm(VNode vnode, std::vector<VNode> insertedVnodeQueue) {
	// TODO: init hook
	if (vnode.sel.compare(std::string("!")) == 0) {
		vnode.elm = createComment(vnode.text);
	} else if (vnode.sel.empty()) {
		vnode.elm = createTextNode(vnode.text);
	} else {
		std::size_t hashIdx = vnode.sel.find('#');
		std::size_t dotIdx = vnode.sel.find('.', hashIdx);
		int hash = hashIdx > 0 ? static_cast<int>(hashIdx) : vnode.sel.length();
		int dot = dotIdx > 0 ? static_cast<int>(dotIdx) : vnode.sel.length();
		std::string tag = hashIdx != -1 || dotIdx != -1 ? vnode.sel.substr(0, std::min(hash, dot)) : vnode.sel;
		vnode.elm = !vnode.data.ns.empty() ? createElementNS(vnode.data.ns, tag) : createElement(tag);

		if (hash < dot) {
			vnode.elm.set("id", emscripten::val(vnode.sel.substr(hash + 1, dot)));
		}
		if (dotIdx > 0) {
			std::string className = vnode.sel.substr(dot + 1);
			std::replace(className.begin(), className.end(), '.', ' ');
			vnode.elm.set("className", emscripten::val(className));
		}
		// TODO: create callback
		if (!vnode.children.empty()) {
			for(std::vector<VNode>::size_type i = 0; i != vnode.children.size(); i++) {
				appendChild(vnode.elm, createElm(vnode.children[i], insertedVnodeQueue));
			}
		} else if (!vnode.text.empty()) {
			appendChild(vnode.elm, createTextNode(vnode.text));
		}
		// TODO: create hook
		// TODO: insert hook
	}
	return vnode.elm;
};

void addVnodes(
	emscripten::val parentElm,
	emscripten::val before,
	std::vector<VNode> vnodes,
	std::vector<VNode>::size_type startIdx,
	std::vector<VNode>::size_type endIdx,
	std::vector<VNode> insertedVnodeQueue
) {
	for (; startIdx <= endIdx; startIdx++) {
		insertBefore(parentElm, createElm(vnodes[startIdx], insertedVnodeQueue), before);
	}
};

void removeVnodes(
	emscripten::val parentElm,
	std::vector<VNode> vnodes,
	std::vector<VNode>::size_type startIdx,
	std::vector<VNode>::size_type endIdx
) {
	for (; startIdx <= endIdx; startIdx++) {
		// TODO: remove callback
		/* if (!vnode.sel.empty()) {
			// TODO: destroy hook
			removeChild(parentElm, vnode.elm);
		} else {*/ 
			removeChild(parentElm, vnodes[startIdx].elm);
		// }
	}
};

void updateChildren(
	emscripten::val parentElm,
	std::vector<VNode> oldCh,
	std::vector<VNode> newCh,
	std::vector<VNode> insertedVnodeQueue
) {
	std::size_t oldStartIdx = 0;
	std::size_t newStartIdx = 0;
	std::size_t oldEndIdx = oldCh.size() - 1;
	std::size_t newEndIdx = newCh.size() - 1;
	VNode oldStartVnode = oldCh[0];
	VNode oldEndVnode = oldCh[oldEndIdx];
	VNode newStartVnode = newCh[0];
	VNode newEndVnode = newCh[newEndIdx];
	std::map<std::string, int> oldKeyToIdx;
	VNode elmToMove;

	while (oldStartIdx <= oldEndIdx && newStartIdx <= newEndIdx) {
		if (sameVnode(oldStartVnode, newStartVnode)) {
			patchVnode(oldStartVnode, newStartVnode, insertedVnodeQueue);
			oldStartVnode = oldCh[++oldStartIdx];
			newStartVnode = newCh[++newStartIdx];
		} else if (sameVnode(oldEndVnode, newEndVnode)) {
			patchVnode(oldEndVnode, newEndVnode, insertedVnodeQueue);
			oldEndVnode = oldCh[--oldEndIdx];
			newEndVnode = newCh[--newEndIdx];
		} else if (sameVnode(oldStartVnode, newEndVnode)) {
			patchVnode(oldStartVnode, newEndVnode, insertedVnodeQueue);
			insertBefore(parentElm, oldStartVnode.elm, nextSibling(oldEndVnode.elm));
			oldStartVnode = oldCh[++oldStartIdx];
			newEndVnode = newCh[--newEndIdx];
		} else if (sameVnode(oldEndVnode, newStartVnode)) {
			patchVnode(oldEndVnode, newStartVnode, insertedVnodeQueue);
			insertBefore(parentElm, oldEndVnode.elm, oldStartVnode.elm);
			oldEndVnode = oldCh[--oldEndIdx];
			newStartVnode = newCh[++newStartIdx];
		} else {
			if (oldKeyToIdx.empty()) {
				oldKeyToIdx = createKeyToOldIdx(oldCh, oldStartIdx, oldEndIdx);
			}
			if (oldKeyToIdx.count(newStartVnode.key) == 0) {
				insertBefore(parentElm, createElm(newStartVnode, insertedVnodeQueue), oldStartVnode.elm);
				newStartVnode = newCh[++newStartIdx];
			} else {
				elmToMove = oldCh[oldKeyToIdx[newStartVnode.key]];
				if (elmToMove.sel.compare(newStartVnode.sel) != 0) {
					insertBefore(parentElm, createElm(newStartVnode, insertedVnodeQueue), oldStartVnode.elm);
				} else {
					patchVnode(elmToMove, newStartVnode, insertedVnodeQueue);
					oldCh[oldKeyToIdx[newStartVnode.key]].key = std::string("");
					insertBefore(parentElm, elmToMove.elm, oldStartVnode.elm);
				}
				newStartVnode = newCh[++newStartIdx];
			}
		}
	}
	if (oldStartIdx > oldEndIdx) {
		addVnodes(parentElm, newCh[newEndIdx+1].elm, newCh, newStartIdx, newEndIdx, insertedVnodeQueue);
	} else if (newStartIdx > newEndIdx) {
		removeVnodes(parentElm, oldCh, oldStartIdx, oldEndIdx);
	}
};

void patchVnode(
	VNode oldVnode,
	VNode vnode,
	std::vector<VNode> insertedVnodeQueue
) {
	// TODO: prepatch hook
	// if (oldVnode == vnode) return;
	// TODO: update hook
	if (vnode.text.empty()) {
		if (!vnode.children.empty() && !oldVnode.children.empty()) {
			// if (vnode.children != oldVnode.children)
			updateChildren(vnode.elm, oldVnode.children, vnode.children, insertedVnodeQueue);
		} else if(!vnode.children.empty()) {
			if (!oldVnode.text.empty()) setTextContent(vnode.elm, std::string(""));
			addVnodes(vnode.elm, emscripten::val::null(), vnode.children, 0, vnode.children.size() - 1, insertedVnodeQueue);
		} else if(!oldVnode.children.empty()) {
			removeVnodes(vnode.elm, oldVnode.children, 0, oldVnode.children.size() - 1);
		} else if (!oldVnode.text.empty()) {
			setTextContent(vnode.elm, oldVnode.text);
		}
	} else if (vnode.text.compare(oldVnode.text) != 0) {
		setTextContent(vnode.elm, vnode.text);
	}
	// TODO: postpatch hook
};

VNode patch_vnode(VNode oldVnode, VNode vnode) {
	std::vector<VNode> insertedVnodeQueue;
	// TODO: pre callback
	if (sameVnode(oldVnode, vnode)) {
		patchVnode(oldVnode, vnode, insertedVnodeQueue);
	} else {
		emscripten::val parent = parentNode(oldVnode.elm);
		createElm(vnode, insertedVnodeQueue);
		if (isDefined(parent)) {
			insertBefore(parent, vnode.elm, nextSibling(oldVnode.elm));
			std::vector<VNode> vnodes;
			vnodes.push_back(oldVnode);
			removeVnodes(parent, vnodes, 0, 0);
		}
	}
	// TODO: insert hook
	// TODO: post callback
	return vnode;
};

VNode patch_element(emscripten::val oldVnode, VNode vnode) {
	return patch_vnode(emptyNodeAt(oldVnode), vnode);
};