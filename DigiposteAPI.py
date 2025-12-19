#!/usr/bin/python3

import os
import sys
import argparse
import requests
import webview
import threading
import time

class AuthAPI:
    def __init__(self):
        self.token = None

    def receive_token(self, token):
        self.token = token
        return "OK"

class DigiposteAPI():
    def __init__(self, token=None):
        self._session = requests.Session()
        
        if token is None:
            self._token = self._authenticate()
            if self._token is None:
                print("No authentication token retrieved")
                exit(1)
        else:
            self._token = token[0]
        
        self._session.headers.update({"Authorization": "Bearer {}".format(self._token), "Accept": "application/json"})
    
    def _authenticate(self):
        def inject_js(window):
            js = f"""
            (function() {{
                try {{
                    const token = sessionStorage.getItem("access_token");
                    if (token) {{
                        window.pywebview.api.receive_token(token);
                    }}
                }} catch (e) {{
                    console.log("Erreur JS:", e);
                }}
            }})();
            """
            window.evaluate_js(js)
        
        api = AuthAPI()

        window = webview.create_window(
            "Connexion Digiposte",
            "https://secure.digiposte.fr/identification-plus",
            js_api=api,
            width=480,
            height=720
        )

        def monitor():
            while api.token is None:
                time.sleep(1)
                inject_js(window)

            window.destroy()

        threading.Thread(target=monitor, daemon=True).start()
        webview.start()
        
        return api.token
    
    def disconnect(self):
        pass
    
    def get_folders_tree(self):
        try:
            resp = self._session.get("https://api.digiposte.fr/api/v3/folders", allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 200:
            print("get_folders_tree() HTTP error code:", resp.status_code)
            return "err"
        
        return resp.text
    
    def get_folder_content(self, folder_id):
        payload = {"locations": ["INBOX", "SAFE"], "folder_id": folder_id}
        
        try:
            resp = self._session.post("https://api.digiposte.fr/api/v3/documents/search?max_results=1000&sort=TITLE", json=payload, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 200:
            print("get_folder_content() HTTP error code:", resp.status_code)
            return "err"
        
        return resp.text
    
    def get_file(self, file_id, dest_path):
        try:
            resp = self._session.get("https://api.digiposte.fr/api/v3/document/{}/content".format(file_id), allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 200:
            print("get_file() HTTP error code:", resp.status_code)
            return "err"
        
        try:
            with open(dest_path, 'wb') as f:
                f.write(resp.content)
        except Exception as e:
            print(e)
            return "err"
        
        return "OK"
    
    def create_folder(self, name, parent_id):
        payload = {"name": "{}".format(name), "favorite": False, "parent_id": "{}".format(parent_id)}
        
        try:
            resp = self._session.post("https://api.digiposte.fr/api/v3/folder", json=payload, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 200:
            print("create_folder() HTTP error code:", resp.status_code)
            return "err"
        
        try:
            return resp.json()["id"]
        except requests.JSONDecodeError as e:
            print(e)
            return "err"
    
    def rename_object(self, object_id, new_name, is_file):
        if is_file:
            url = "https://api.digiposte.fr/api/v3/document/{}/rename/{}".format(object_id, new_name)
        else:
            url = "https://api.digiposte.fr/api/v3/folder/{}/rename/{}".format(object_id, new_name)
        
        try:
            resp = self._session.put(url, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 200:
            print("rename_object() HTTP error code:", resp.status_code)
            return "err"
        
        return resp.text
    
    def delete_object(self, object_id, is_file):
        if is_file:
            payload = {"document_ids": ["{}".format(object_id)], "folder_ids": []}
        else:
            payload = {"document_ids": [], "folder_ids": ["{}".format(object_id)]}
        
        try:
            resp = self._session.post("https://api.digiposte.fr/api/v3/file/tree/trash", json=payload, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 204:
            print("delete_object() HTTP error code:", resp.status_code)
            return "err"
        
        return resp.text
    
    def move_object(self, object_id, dest_folder_id, is_file):
        if is_file:
            payload = {"document_ids": ["{}".format(object_id)], "folder_ids": []}
        else:
            payload = {"document_ids": [], "folder_ids": ["{}".format(object_id)]}
        
        try:
            resp = self._session.put("https://api.digiposte.fr/api/v3/file/tree/move", params={"to": dest_folder_id}, json=payload, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 204:
            print("move_object() HTTP error code:", resp.status_code)
            return "err"
        
        return resp.text
    
    def upload_file(self, dest_folder_id, src_file_path, name, size):
        try:
            f = open(src_file_path, 'rb')
        except Exception as e:
            print(e)
            return "err"
        
        if dest_folder_id is None:
            payload = {"archive_size": size, "archive": (name, f), "health_document": False, "title": name}
        else:
            payload = {"archive_size": size, "archive": (name, f), "health_document": False, "title": name, "folder_id": dest_folder_id}
        
        try:
            resp = self._session.post("https://api.digiposte.fr/api/v3/document", files=payload, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(e)
            return "err"
        
        if resp.status_code != 200:
            print("upload_file() HTTP error code:", resp.status_code)
            return "err"
        
        f.close()
        try:
            return resp.json()["id"]
        except requests.JSONDecodeError as e:
            print(e)
            return "err"

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="DigiposteAPI", description="Digiposte API communication")
    parser.add_argument("--server", nargs=2, metavar=("read_fd", "write_fd"), type=int, required=False, help="Spawn DigiposteAPI as a server with anonymous pipe passed as arguments")
    parser.add_argument("--token", nargs=1, metavar="token", required=False, help="Authentication token for Digiposte API. Not recommended")
    #parser.add_argument("--retry", nargs=1, type=int, required=False, default=3, help="Number of retries when API returns an error. Default to 3")
    #parser.add_argument("--cli", action='store_true', default=False, help="Prompts will be printed in terminal, otherwise use UI")
    
    subparsers = parser.add_subparsers(metavar="action", dest="action", required=False, help='Action to perform')
    parser_get_folders_tree = subparsers.add_parser('get_folders_tree', help='Get folders tree')
    
    parser_get_folder_content = subparsers.add_parser('get_folder_content', help='Get folder content')
    parser_get_folder_content.add_argument("folder_id", help="Folder ID to retrieve content from")
    
    parser_get_file = subparsers.add_parser('get_file', help='Get file')
    parser_get_file.add_argument("file_id", help="File ID to download")
    parser_get_file.add_argument("dest_path", help="Destination path for the downloaded file")
    
    parser_create_folder = subparsers.add_parser('create_folder', help='Create folder')
    parser_create_folder.add_argument("name", help="Name of the new folder")
    parser_create_folder.add_argument("parent_id", help="Folder ID of the parent folder")
    
    parser_rename_object = subparsers.add_parser('rename_object', help='Rename object')
    parser_rename_object.add_argument("--file", required=False, help="The object is a file")
    parser_rename_object.add_argument("object_id", help="Object ID to rename")
    parser_rename_object.add_argument("new_name", help="New name of the object")
    
    parser_delete_object = subparsers.add_parser('delete_object', help='Delete object')
    parser_delete_object.add_argument("--file", required=False, help="The object is a file")
    parser_delete_object.add_argument("object_id", help="Object ID to delete")
    
    parser_move_object = subparsers.add_parser('move_object', help='Move object')
    parser_move_object.add_argument("--file", required=False, help="The object is a file")
    parser_move_object.add_argument("object_id", help="Object ID to move")
    parser_move_object.add_argument("dest_folder_id", help="Destination folder ID")
    
    parser_upload_file = subparsers.add_parser('upload_file', help='Upload file')
    parser_upload_file.add_argument("dest_folder_id", help="Destination folder ID")
    parser_upload_file.add_argument("src_file_path", help="Path of the file to upload")
    parser_upload_file.add_argument("name", help="Name of the file to upload")
    parser_upload_file.add_argument("size", type=int, help="Size of the file to upload in bytes")
    
    args = parser.parse_args()
    
    dgp_api = DigiposteAPI(token=args.token)
    
    if args.server:
        read_f = os.fdopen(args.server[0], mode='rb')
        write_fd = args.server[1]
        
        os.write(write_fd, b"ready" + b'\0')
        
        buffer = read_f.readline()
        while buffer != b"":
            com = buffer[:-1].split(b'\0')
            if com[0] == b"get_folders_tree":
                tree = dgp_api.get_folders_tree()
                os.write(write_fd, tree.encode() + b'\0')
            
            elif com[0] == b"get_folder_content":
                content = dgp_api.get_folder_content(com[1].decode())
                os.write(write_fd, content.encode() + b'\0')
            
            elif com[0] == b"get_file":
                if dgp_api.get_file(com[1].decode(), com[2].decode()) == "err":
                    os.write(write_fd, b'err' + b'\0')
                else:
                    os.write(write_fd, b'OK' + b'\0')
            
            elif com[0] == b"create_folder":
                folder_id = dgp_api.create_folder(com[1].decode(), com[2].decode())
                os.write(write_fd, folder_id.encode() + b'\0')
            
            elif com[0] == b"rename_object":
                is_file = com[1] == b'1'
                if dgp_api.rename_object(com[2].decode(), com[3].decode(), is_file) == "err":
                    os.write(write_fd, b'err' + b'\0')
                else:
                    os.write(write_fd, b'OK' + b'\0')
            
            elif com[0] == b"delete_object":
                is_file = com[1] == b'1'
                if dgp_api.delete_object(com[2].decode(), is_file) == "err":
                    os.write(write_fd, b'err' + b'\0')
                else:
                    os.write(write_fd, b'OK' + b'\0')
            
            elif com[0] == b"move_object":
                is_file = com[1] == b'1'
                if com[3] == b'':
                    dest_folder_id = None
                else:
                    dest_folder_id = com[3].decode()
                
                if dgp_api.move_object(com[2].decode(), dest_folder_id, is_file) == "err":
                    os.write(write_fd, b'err' + b'\0')
                else:
                    os.write(write_fd, b'OK' + b'\0')
            
            elif com[0] == b"upload_file":
                if com[1] == b'':
                    dest_folder_id = None
                else:
                    dest_folder_id = com[1].decode()
                file_id = dgp_api.upload_file(dest_folder_id, com[2].decode(), com[3].decode(), com[4].decode())
                os.write(write_fd, file_id.encode() + b'\0')
            
            else:
                print("Unknown command", com[0], "with parameters", buffer[:-1].split(b'\0')[1:])
                os.write(write_fd, b'err' + b'\0')
            
            buffer = read_f.readline()
        
        print("Pipe closed by client. Exiting...")
        dgp_api.disconnect()
        read_f.close()
        os.close(write_fd)
        
    else:
        if args.action == "get_folders_tree":
            print(dgp_api.get_folders_tree())
        elif args.action == "get_folder_content":
            pass
        elif args.action == "get_file":
            pass
        elif args.action == "create_folder":
            pass
        elif args.action == "rename_object":
            pass
        elif args.action == "delete_object":
            pass
        elif args.action == "move_object":
            pass
        elif args.action == "upload_file":
            pass
        else:
            print("Error")
