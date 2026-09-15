const manifest = {"name":"BC-250 Sleep"};
const API_VERSION = 2;
const internalAPIConnection = window.__DECKY_SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED_deckyLoaderAPIInit;
if (!internalAPIConnection) {
    throw new Error('[@decky/api]: Failed to connect to the loader as as the loader API was not initialized. This is likely a bug in Decky Loader.');
}
let api;
try {
    api = internalAPIConnection.connect(API_VERSION, manifest.name);
}
catch {
    api = internalAPIConnection.connect(1, manifest.name);
    console.warn(`[@decky/api] Requested API version ${API_VERSION} but the running loader only supports version 1. Some features may not work.`);
}
if (api._version != API_VERSION) {
    console.warn(`[@decky/api] Requested API version ${API_VERSION} but the running loader only supports version ${api._version}. Some features may not work.`);
}
const callable = api.callable;
const toaster = api.toaster;
const definePlugin = (fn) => {
    return (...args) => {
        return fn(...args);
    };
};

var DefaultContext = {
  color: undefined,
  size: undefined,
  className: undefined,
  style: undefined,
  attr: undefined
};
var IconContext = SP_REACT.createContext && /*#__PURE__*/SP_REACT.createContext(DefaultContext);

var _excluded = ["attr", "size", "title"];
function _objectWithoutProperties(source, excluded) { if (source == null) return {}; var target = _objectWithoutPropertiesLoose(source, excluded); var key, i; if (Object.getOwnPropertySymbols) { var sourceSymbolKeys = Object.getOwnPropertySymbols(source); for (i = 0; i < sourceSymbolKeys.length; i++) { key = sourceSymbolKeys[i]; if (excluded.indexOf(key) >= 0) continue; if (!Object.prototype.propertyIsEnumerable.call(source, key)) continue; target[key] = source[key]; } } return target; }
function _objectWithoutPropertiesLoose(source, excluded) { if (source == null) return {}; var target = {}; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { if (excluded.indexOf(key) >= 0) continue; target[key] = source[key]; } } return target; }
function _extends() { _extends = Object.assign ? Object.assign.bind() : function (target) { for (var i = 1; i < arguments.length; i++) { var source = arguments[i]; for (var key in source) { if (Object.prototype.hasOwnProperty.call(source, key)) { target[key] = source[key]; } } } return target; }; return _extends.apply(this, arguments); }
function ownKeys(e, r) { var t = Object.keys(e); if (Object.getOwnPropertySymbols) { var o = Object.getOwnPropertySymbols(e); r && (o = o.filter(function (r) { return Object.getOwnPropertyDescriptor(e, r).enumerable; })), t.push.apply(t, o); } return t; }
function _objectSpread(e) { for (var r = 1; r < arguments.length; r++) { var t = null != arguments[r] ? arguments[r] : {}; r % 2 ? ownKeys(Object(t), true).forEach(function (r) { _defineProperty(e, r, t[r]); }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) { Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r)); }); } return e; }
function _defineProperty(obj, key, value) { key = _toPropertyKey(key); if (key in obj) { Object.defineProperty(obj, key, { value: value, enumerable: true, configurable: true, writable: true }); } else { obj[key] = value; } return obj; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == typeof i ? i : i + ""; }
function _toPrimitive(t, r) { if ("object" != typeof t || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r); if ("object" != typeof i) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
function Tree2Element(tree) {
  return tree && tree.map((node, i) => /*#__PURE__*/SP_REACT.createElement(node.tag, _objectSpread({
    key: i
  }, node.attr), Tree2Element(node.child)));
}
function GenIcon(data) {
  return props => /*#__PURE__*/SP_REACT.createElement(IconBase, _extends({
    attr: _objectSpread({}, data.attr)
  }, props), Tree2Element(data.child));
}
function IconBase(props) {
  var elem = conf => {
    var {
        attr,
        size,
        title
      } = props,
      svgProps = _objectWithoutProperties(props, _excluded);
    var computedSize = size || conf.size || "1em";
    var className;
    if (conf.className) className = conf.className;
    if (props.className) className = (className ? className + " " : "") + props.className;
    return /*#__PURE__*/SP_REACT.createElement("svg", _extends({
      stroke: "currentColor",
      fill: "currentColor",
      strokeWidth: "0"
    }, conf.attr, attr, svgProps, {
      className: className,
      style: _objectSpread(_objectSpread({
        color: props.color || conf.color
      }, conf.style), props.style),
      height: computedSize,
      width: computedSize,
      xmlns: "http://www.w3.org/2000/svg"
    }), title && /*#__PURE__*/SP_REACT.createElement("title", null, title), props.children);
  };
  return IconContext !== undefined ? /*#__PURE__*/SP_REACT.createElement(IconContext.Consumer, null, conf => elem(conf)) : elem(DefaultContext);
}

// THIS FILE IS AUTO GENERATED
function FaMoon (props) {
  return GenIcon({"attr":{"viewBox":"0 0 512 512"},"child":[{"tag":"path","attr":{"d":"M283.211 512c78.962 0 151.079-35.925 198.857-94.792 7.068-8.708-.639-21.43-11.562-19.35-124.203 23.654-238.262-71.576-238.262-196.954 0-72.222 38.662-138.635 101.498-174.394 9.686-5.512 7.25-20.197-3.756-22.23A258.156 258.156 0 0 0 283.211 0c-141.309 0-256 114.511-256 256 0 141.309 114.511 256 256 256z"},"child":[]}]})(props);
}

const getStatus = callable("status");
const setSetting = callable("set_setting");
const sleepNow = callable("sleep");
const wakeNow = callable("wake");
// Steam's own Sleep entry ends in SteamClient.System.SuspendPC(), which asks logind to suspend and
// hangs the BC-250. While the option is on we replace that function with our fake sleep (and the
// backend masks the systemd sleep units as a safety net, so a real suspend can never be reached).
// This Steam build has no RegisterForOnSuspendRequest, so wrapping the call is the only hook.
let originalSuspendPC = null;
function installSuspendHook(on) {
    if (typeof SteamClient === "undefined" || !SteamClient?.System)
        return;
    if (on && !originalSuspendPC && typeof SteamClient.System.SuspendPC === "function") {
        originalSuspendPC = SteamClient.System.SuspendPC;
        SteamClient.System.SuspendPC = async (..._args) => {
            console.log("[bc250-sleep] Steam Sleep -> fake sleep");
            try {
                await sleepNow();
            }
            catch (e) {
                console.error(e);
            }
        };
    }
    else if (!on && originalSuspendPC) {
        SteamClient.System.SuspendPC = originalSuspendPC;
        originalSuspendPC = null;
    }
}
function Content() {
    const [st, setSt] = SP_REACT.useState(null);
    const [busy, setBusy] = SP_REACT.useState(false);
    const refresh = async () => {
        try {
            setSt(await getStatus());
        }
        catch (e) {
            console.error(e);
        }
    };
    SP_REACT.useEffect(() => {
        refresh();
        const t = setInterval(refresh, 2000);
        return () => clearInterval(t);
    }, []);
    const act = async (label, fn) => {
        setBusy(true);
        try {
            const r = await fn();
            if (!r.ok)
                toaster.toast({ title: "BC-250 Sleep", body: `${label}: ${r.output}` });
        }
        catch (e) {
            toaster.toast({ title: "BC-250 Sleep", body: `${label} error: ${e}` });
        }
        setBusy(false);
        refresh();
    };
    const toggle = (key) => async (v) => {
        await act(key, () => setSetting(key, v));
        if (key === "hook_steam_sleep")
            installSuspendHook(v);
    };
    if (!st)
        return SP_JSX.jsx(DFL.PanelSection, { children: SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.Field, { label: "Loading\u2026" }) }) });
    const s = st.settings;
    return (SP_JSX.jsxs(SP_JSX.Fragment, { children: [SP_JSX.jsxs(DFL.PanelSection, { title: "Sleep", children: [SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ButtonItem, { layout: "below", disabled: busy || st.asleep, onClick: () => act("sleep", sleepNow), children: "Sleep now" }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.Field, { label: "State", focusable: true, children: st.asleep ? `asleep, ${st.frozen} processes frozen` : st.game ? `awake, game running (app ${st.game.appid})` : "awake, no game running" }) }), st.asleep && (SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ButtonItem, { layout: "below", disabled: busy, onClick: () => act("wake", wakeNow), children: "Wake" }) })), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.Field, { focusable: true, description: "The BC-250 cannot really sleep (no S3). This freezes the game, mutes audio and turns the TV off; any button press brings it back where you left it. The board stays on at its idle power." }) })] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Options", children: [SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ToggleField, { label: "Freeze the running game", description: "SIGSTOP the game's processes; resume on wake", checked: s.pause_game, disabled: busy, onChange: toggle("pause_game") }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ToggleField, { label: "Mute audio", checked: s.mute_audio, disabled: busy, onChange: toggle("mute_audio") }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ToggleField, { label: "Wake on any input", description: "Any controller, keyboard or mouse button press wakes", checked: s.wake_on_input, disabled: busy, onChange: toggle("wake_on_input") }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ToggleField, { label: "Use Steam's Sleep button", description: `Steam's Sleep runs this instead of the (broken) system suspend. Masks systemd sleep units. ${st.sleep_masked ? "Currently masked." : "Currently not masked."}`, checked: s.hook_steam_sleep, disabled: busy, onChange: toggle("hook_steam_sleep") }) })] })] }));
}
var index = definePlugin(() => {
    getStatus().then((st) => installSuspendHook(!!st?.settings?.hook_steam_sleep)).catch(console.error);
    return {
        name: "BC-250 Sleep",
        titleView: SP_JSX.jsx("div", { className: DFL.staticClasses.Title, children: "BC-250 Sleep" }),
        content: SP_JSX.jsx(Content, {}),
        icon: SP_JSX.jsx(FaMoon, {}),
        onDismount() { installSuspendHook(false); },
    };
});

export { index as default };
//# sourceMappingURL=index.js.map
