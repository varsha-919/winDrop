# Windrop

Windrop is a React application built with Vite, providing a fast, modern development experience with Hot Module Replacement (HMR) and ESLint for code quality.

## Tech Stack

* React
* Vite
* JavaScript
* ESLint

## Development

This project uses Vite for fast builds and development.

Currently, two official React plugins are available:

* [@vitejs/plugin-react](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react) — Uses **Oxc**
* [@vitejs/plugin-react-swc](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react-swc) — Uses **SWC**

## React Compiler

The React Compiler is not enabled by default because it can impact development and build performance. If you'd like to enable it, refer to the official React documentation:

https://react.dev/learn/react-compiler/installation

## ESLint Configuration

For production applications, consider migrating to TypeScript with type-aware linting enabled. More information is available in the official Vite React TypeScript template:

https://github.com/vitejs/vite/tree/main/packages/create-vite/template-react-ts
