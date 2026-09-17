const manifest = {"name":"BC-250 Tune"};
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
function _objectWithoutProperties(e, t) { if (null == e) return {}; var o, r, i = _objectWithoutPropertiesLoose(e, t); if (Object.getOwnPropertySymbols) { var n = Object.getOwnPropertySymbols(e); for (r = 0; r < n.length; r++) o = n[r], -1 === t.indexOf(o) && {}.propertyIsEnumerable.call(e, o) && (i[o] = e[o]); } return i; }
function _objectWithoutPropertiesLoose(r, e) { if (null == r) return {}; var t = {}; for (var n in r) if ({}.hasOwnProperty.call(r, n)) { if (-1 !== e.indexOf(n)) continue; t[n] = r[n]; } return t; }
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
function ownKeys(e, r) { var t = Object.keys(e); if (Object.getOwnPropertySymbols) { var o = Object.getOwnPropertySymbols(e); r && (o = o.filter(function (r) { return Object.getOwnPropertyDescriptor(e, r).enumerable; })), t.push.apply(t, o); } return t; }
function _objectSpread(e) { for (var r = 1; r < arguments.length; r++) { var t = null != arguments[r] ? arguments[r] : {}; r % 2 ? ownKeys(Object(t), true).forEach(function (r) { _defineProperty(e, r, t[r]); }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) { Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r)); }); } return e; }
function _defineProperty(e, r, t) { return (r = _toPropertyKey(r)) in e ? Object.defineProperty(e, r, { value: t, enumerable: true, configurable: true, writable: true }) : e[r] = t, e; }
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
    var attr = props.attr,
      size = props.size,
      title = props.title,
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
function FaMicrochip (props) {
  return GenIcon({"attr":{"viewBox":"0 0 512 512"},"child":[{"tag":"path","attr":{"d":"M416 48v416c0 26.51-21.49 48-48 48H144c-26.51 0-48-21.49-48-48V48c0-26.51 21.49-48 48-48h224c26.51 0 48 21.49 48 48zm96 58v12a6 6 0 0 1-6 6h-18v6a6 6 0 0 1-6 6h-42V88h42a6 6 0 0 1 6 6v6h18a6 6 0 0 1 6 6zm0 96v12a6 6 0 0 1-6 6h-18v6a6 6 0 0 1-6 6h-42v-48h42a6 6 0 0 1 6 6v6h18a6 6 0 0 1 6 6zm0 96v12a6 6 0 0 1-6 6h-18v6a6 6 0 0 1-6 6h-42v-48h42a6 6 0 0 1 6 6v6h18a6 6 0 0 1 6 6zm0 96v12a6 6 0 0 1-6 6h-18v6a6 6 0 0 1-6 6h-42v-48h42a6 6 0 0 1 6 6v6h18a6 6 0 0 1 6 6zM30 376h42v48H30a6 6 0 0 1-6-6v-6H6a6 6 0 0 1-6-6v-12a6 6 0 0 1 6-6h18v-6a6 6 0 0 1 6-6zm0-96h42v48H30a6 6 0 0 1-6-6v-6H6a6 6 0 0 1-6-6v-12a6 6 0 0 1 6-6h18v-6a6 6 0 0 1 6-6zm0-96h42v48H30a6 6 0 0 1-6-6v-6H6a6 6 0 0 1-6-6v-12a6 6 0 0 1 6-6h18v-6a6 6 0 0 1 6-6zm0-96h42v48H30a6 6 0 0 1-6-6v-6H6a6 6 0 0 1-6-6v-12a6 6 0 0 1 6-6h18v-6a6 6 0 0 1 6-6z"},"child":[]}]})(props);
}

const getStatus = callable("status");
const setOption = callable("set_option");
const warmReboot = callable("reboot");
const restartSession = callable("restart_session");
const opt = (data, label) => ({ data, label });
function Content() {
    const [st, setSt] = SP_REACT.useState(null);
    const [busy, setBusy] = SP_REACT.useState(false);
    const [err, setErr] = SP_REACT.useState(null);
    const refresh = async () => {
        try {
            const s = await getStatus();
            if (s) {
                setSt(s);
                setErr(null);
            }
            else
                setErr("bc250-tune not installed or status failed");
        }
        catch (e) {
            setErr(String(e));
        }
    };
    SP_REACT.useEffect(() => {
        refresh();
        const t = setInterval(refresh, 3000);
        return () => clearInterval(t);
    }, []);
    const apply = async (key, value) => {
        setBusy(true);
        try {
            const r = await setOption(key, value);
            toaster.toast({
                title: "BC-250 Tune",
                body: r.ok ? `${key} = ${value} applied` : `failed: ${r.output.trim().split("\n").pop()}`,
            });
        }
        catch (e) {
            toaster.toast({ title: "BC-250 Tune", body: `error: ${e}` });
        }
        setBusy(false);
        refresh();
    };
    const runAction = async (label, fn) => {
        setBusy(true);
        try {
            const r = await fn();
            if (!r.ok)
                toaster.toast({ title: "BC-250 Tune", body: `${label} failed: ${r.output.trim().split("\n").pop()}` });
        }
        catch (e) {
            toaster.toast({ title: "BC-250 Tune", body: `${label} error: ${e}` });
        }
        setBusy(false);
    };
    if (!st) {
        return (SP_JSX.jsx(DFL.PanelSection, { children: SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.Field, { label: err ?? "Loading…" }) }) }));
    }
    const pending = [];
    if (st.pending.reboot)
        pending.push(`reboot: ${st.pending.reboot.replace(/;$/, "")}`);
    if (st.pending.session_restart)
        pending.push("restart gaming session (resolution)");
    if (st.pending.cold_boot)
        pending.push("cold boot (power off) to return to 6 cores");
    return (SP_JSX.jsxs(SP_JSX.Fragment, { children: [SP_JSX.jsxs(DFL.PanelSection, { title: "Live", children: [SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsxs(DFL.Field, { label: "GPU", focusable: true, children: [st.gpu.cur_mhz_estimated ? "~" : "", st.gpu.cur_mhz, " MHz \u00B7 ", st.gpu.temp, "\u00B0C \u00B7 ", st.cu.live, " CU"] }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsxs(DFL.Field, { label: "CPU", focusable: true, children: [st.cores.visible, " cores \u00B7 ", st.power.cpu_temp, "\u00B0C"] }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsxs(DFL.Field, { label: "Power", focusable: true, children: ["SoC ", st.power.soc_w, " W \u00B7 total ~", st.power.total_w, " W"] }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsxs(DFL.Field, { label: "VRAM", focusable: true, children: [st.uma.live_mb, " MB", st.res.session ? ` · ${st.res.session}` : ""] }) }), pending.map((p) => (SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.Field, { label: "Pending", focusable: true, children: p }) }, p)))] }), SP_JSX.jsxs(DFL.PanelSection, { title: "GPU", children: [SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.DropdownItem, { label: "Compute units", description: "Live, 2 per WGP. 32 and 40 are symmetric across the shader rows; step down until stable. 40: +~30 W, ~+5 % in games, 1.6x compute", rgOptions: [24, 26, 28, 30, 32, 34, 36, 38, 40].map((n) => opt(n, n === 24 ? "24 (stock)" : n === 32 ? "32 (symmetric)" : n === 40 ? "40 (all WGPs)" : String(n))), selectedOption: st.cu.config, disabled: busy, onChange: (o) => apply("cu", String(o.data)) }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.DropdownItem, { label: "Max clock", description: "Governor ceiling (live)", rgOptions: [opt(1500, "1500 MHz"), opt(1700, "1700 MHz"), opt(1850, "1850 MHz (default)"), opt(2000, "2000 MHz (hot)")], selectedOption: st.gpu.config_max, disabled: busy, onChange: (o) => apply("gpu-max", String(o.data)) }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.DropdownItem, { label: "Idle clock", description: "Governor floor (live). 500 saves ~10 W at idle", rgOptions: [opt(500, "500 MHz"), opt(1000, "1000 MHz")], selectedOption: st.gpu.config_min, disabled: busy, onChange: (o) => apply("gpu-min", String(o.data)) }) })] }), SP_JSX.jsxs(DFL.PanelSection, { title: "CPU", children: [SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ToggleField, { label: "8 cores", description: "SMU core unlock. Needs a warm reboot; a cold boot (power off) reverts to 6", checked: st.cores.config === 8, disabled: busy, onChange: (v) => apply("cores", v ? "8" : "6") }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ToggleField, { label: "Auto warm reboot after power-on", description: "A cold boot always comes up with 6 cores; with this on, the boot service reboots once by itself (adds ~40 s)", checked: st.cores.auto_reboot === "on", disabled: busy, onChange: (v) => apply("cores-auto-reboot", v ? "on" : "off") }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ButtonItem, { layout: "below", disabled: busy, onClick: () => runAction("reboot", warmReboot), children: "Warm reboot now" }) })] }), SP_JSX.jsx(DFL.PanelSection, { title: "Memory", children: SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.DropdownItem, { label: "VRAM (UMA split)", description: "Written to CMOS, applies at next reboot. Rest of the 16 GB is system RAM", rgOptions: [512, 1024, 2048, 3072, 4096, 6144, 8192, 10240, 12288].map((m) => opt(m, `${m} MB`)), selectedOption: st.uma.config, disabled: busy, onChange: (o) => apply("uma", String(o.data)) }) }) }), SP_JSX.jsxs(DFL.PanelSection, { title: "Display & HUD", children: [SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ToggleField, { label: "Custom HUD line", description: "Installs the one-line MangoHud layout as Performance Overlay level 1", checked: st.hud.config === "on", disabled: busy, onChange: (v) => apply("hud", v ? "on" : "off") }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.DropdownItem, { label: "Gaming Mode resolution", description: "Applies when the gaming session restarts", rgOptions: [opt("1920x1080", "1080p"), opt("2560x1440", "1440p"), opt("3840x2160", "4K"), opt("native", "native")], selectedOption: st.res.config, disabled: busy, onChange: (o) => apply("res", String(o.data)) }) }), SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ButtonItem, { layout: "below", disabled: busy, onClick: () => runAction("session restart", restartSession), children: "Restart gaming session (closes game)" }) })] })] }));
}
var index = definePlugin(() => ({
    name: "BC-250 Tune",
    titleView: SP_JSX.jsx("div", { className: DFL.staticClasses.Title, children: "BC-250 Tune" }),
    content: SP_JSX.jsx(Content, {}),
    icon: SP_JSX.jsx(FaMicrochip, {}),
    onDismount() { },
}));

export { index as default };
//# sourceMappingURL=index.js.map
