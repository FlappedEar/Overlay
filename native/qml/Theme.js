.pragma library

// The editor's look, in FlappedEar Telemetry's design language: a dark
// track-day dashboard of flat charcoal panels with FlappedEar amber as the one
// accent. Values mirror FetTheme.dark() and FetColors in FlappedEar/Telemetry
// lib/ui/theme.dart; see docs/ui-theme.md for the mapping.
//
// Screens use these names, never their own hex values. Overlay widgets drawn
// into the video keep their own colours: those are the user's design.
//
// A JavaScript library rather than a QML singleton, so the relative import
// works from the module, from files loaded by path (tests) and from the
// user-guide capture alike.

// Fonts, bundled under native/resources/fonts (SIL OFL 1.1).
var sans = "Sora";
var mono = "JetBrains Mono";
// Tabular digits, so numbers line up in columns.
var numbers = { "tnum": 1 };

// FlappedEar amber, from the brand logo.
var amber = "#fcb203";

// Colour roles (Material 3 names, as in Telemetry).
var primary = amber;
var onPrimary = "#1a1200";
var primaryHover = "#ffc22e";
var primaryPressed = "#e0a000";
var primaryContainer = "#3a2c00";
var onPrimaryContainer = "#ffe08a";
var secondary = "#3d8bff";
var onSecondary = "#04122b";
var secondaryContainer = "#0f2a52";
var onSecondaryContainer = "#d6e6ff";
var tertiary = "#53bc94";
var onTertiary = "#00281a";
var error = "#ff6b5c";
var onError = "#2b0500";
var errorContainer = "#4d1610";
var onErrorContainer = "#ffdad5";

var surface = "#111214";
var onSurface = "#f2f2f2";
var onSurfaceVariant = "#a3a6ad";
var surfaceContainerLowest = "#0b0c0e";
var surfaceContainerLow = "#16171a";
var surfaceContainer = "#1b1c1f";
var surfaceContainerHigh = "#24262a";
var surfaceContainerHighest = "#2a2c31";
var outline = "#5d6068";
var outlineVariant = "#2e3035";

// Lap and timing colours (FetColors).
var you = amber;
var reference = "#3d8bff";
var loss = "#ff6b5c";
var gain = "#53bc94";
var dayBest = "#b37bff";
var onLap = "#111214";

// Shape.
var radius = 3;
var dialogRadius = 6;

// Type scale for a desktop editor (pixel sizes).
var labelSmall = 10;
var labelMedium = 11;
var body = 12;
var titleSmall = 13;
var titleMedium = 15;
var titleLarge = 18;
