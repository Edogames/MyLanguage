const vscode = require('vscode');
const path = require('path');

function activate(context) {
	console.log('Congratulations, your extension "vscode-mylang-extension" is now active!');

	// Example: Register a command that prints a message
	let disposableHello = vscode.commands.registerCommand('vscode-mylang-extension.helloWorld', () => {
		vscode.window.showInformationMessage('Hello World from MyLang!');
	});

	// Compile command
	let disposableCompile = vscode.commands.registerCommand('vscode-mylang-extension.compile', () => {
		const editor = vscode.window.activeTextEditor;
		if (!editor || !editor.document.fileName.endsWith('.mlg')) {
			vscode.window.showErrorMessage('Please open a .mlg file to compile.');
			return;
		}

		const filePath = editor.document.fileName;
		const workspaceFolder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
		if (!workspaceFolder) {
			vscode.window.showErrorMessage('No workspace folder found.');
			return;
		}

		const compilerPath = path.join(workspaceFolder.uri.fsPath, 'mylang.exe'); // Assuming mylang.exe is in workspace root
		const terminal = vscode.window.createTerminal('MyLang Compiler');
		terminal.show();
		terminal.sendText(`"${compilerPath}" "${filePath}"`);
	});

	// Completion provider for IntelliSense
	let completionProvider = vscode.languages.registerCompletionItemProvider('mylang', {
		provideCompletionItems(document, position) {
			const linePrefix = document.lineAt(position).text.substr(0, position.character);
			const items = [];

			// Keywords
			const keywords = ['void', 'string', 'if', 'else', 'while', 'for', 'return', 'from', 'import'];
			keywords.forEach(keyword => {
				const item = new vscode.CompletionItem(keyword, vscode.CompletionItemKind.Keyword);
				item.detail = `MyLang keyword: ${keyword}`;
				items.push(item);
			});

			// Built-in functions
			const functions = ['print', 'input', 'strcmp', 'string_has', 'clear_console'];
			functions.forEach(func => {
				const item = new vscode.CompletionItem(func, vscode.CompletionItemKind.Function);
				item.detail = `MyLang function: ${func}`;
				item.insertText = new vscode.SnippetString(`${func}($1)`);
				items.push(item);
			});

			return items;
		}
	});

	// Hover provider for IntelliSense
	let hoverProvider = vscode.languages.registerHoverProvider('mylang', {
		provideHover(document, position) {
			const wordRange = document.getWordRangeAtPosition(position);
			const word = document.getText(wordRange);

			const hovers = {
				'void': 'Keyword: Defines a function that returns nothing.',
				'string': 'Type: Represents a string of characters.',
				'if': 'Keyword: Conditional statement.',
				'else': 'Keyword: Alternative branch for if statement.',
				'print': 'Function: Prints a string to the console.',
				'input': 'Function: Reads a string from user input.',
				'strcmp': 'Function: Compares two strings.',
				'string_has': 'Function: Checks if a string contains a substring.',
				'clear_console': 'Function: Clears the console.'
			};

			if (hovers[word]) {
				return new vscode.Hover(hovers[word]);
			}
		}
	});

	context.subscriptions.push(disposableHello);
	context.subscriptions.push(disposableCompile);
	context.subscriptions.push(completionProvider);
	context.subscriptions.push(hoverProvider);
}

function deactivate() {}

module.exports = {
	activate,
	deactivate
};