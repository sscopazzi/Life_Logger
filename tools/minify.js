// dashboard.html -> minified html (script through terser, CSS whitespace squeezed)
const fs = require('fs'), {minify} = require('terser');
(async () => {
  const [src, out] = process.argv.slice(2);
  let h = fs.readFileSync(src, 'utf8');
  const a = h.indexOf('<script>') + 8, b = h.indexOf('</script>', a);
  const js = (await minify(h.slice(a, b), {compress: {passes: 2}, mangle: {toplevel: false}, format: {comments: false}})).code;
  h = h.slice(0, a) + js + h.slice(b);
  h = h.replace(/<style>([\s\S]*?)<\/style>/, (m, css) => '<style>' + css.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\s*\n\s*/g, '').replace(/\s*([{};:,>])\s*/g, '$1').replace(/;}/g, '}') + '</style>');
  fs.writeFileSync(out, h);
  console.log(fs.statSync(src).size, '->', h.length);
})();
