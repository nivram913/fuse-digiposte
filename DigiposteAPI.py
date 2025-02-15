#!/usr/bin/python3

import os
import sys
import argparse
import requests

class DigiposteAPI():
    def __init__(self, token=None):
        self._session = requests.Session()
        
        if token is None:
            self._token = self._authenticate()
        else:
            self._token = token[0]
        
        self._session.headers.update({"Authorization": "Bearer {}".format(self._token), "Accept": "application/json"})
    
    def _authenticate(self):
        return "xxx"
    
    def disconnect(self):
        pass
    
    def get_folders_tree(self):
        try:
            resp = self._session.get("https://api.digiposte.fr/api/v3/folders", allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(err)
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
            print(err)
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
            print(err)
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
            print(err)
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
            print(err)
            return "err"
        
        if resp.status_code != 200:
            print("rename_object() HTTP error code:", resp.status_code)
            return "err"
        
        return resp.text
    
    def delete_object(self, object_id, is_file):
        if is_file:
            payload = {"document_ids": ["{}".fomat(object_id)], "folder_ids": []}
        else:
            payload = {"document_ids": [], "folder_ids": ["{}".fomat(object_id)]}
        
        try:
            resp = self._session.post("https://api.digiposte.fr/api/v3/file/tree/trash", json=payload, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(err)
            return "err"
        
        if resp.status_code != 200:
            print("delete_object() HTTP error code:", resp.status_code)
            return "err"
        
        return resp.text
    
    def move_object(self, object_id, dest_folder_id, is_file):
        if is_file:
            payload = {"document_ids": ["{}".fomat(object_id)], "folder_ids": []}
        else:
            payload = {"document_ids": [], "folder_ids": ["{}".fomat(object_id)]}
        
        try:
            resp = self._session.put("https://api.digiposte.fr/api/v3/file/tree/move", params={"to": dest_folder_id}, json=payload, allow_redirects=False)
        except requests.Timeout:
            return "err"
        except (requests.RequestException, requests.ConnectionError, requests.TooManyRedirects) as e:
            print(err)
            return "err"
        
        if resp.status_code != 200:
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
            print(err)
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
        write_f = os.fdopen(args.server[1], mode='w')
        
        buffer = read_f.readline()
        while buffer != "":
            com = buffer[:-1].split(b'\0')
            if com[0] == "get_folders_tree":
                tree = dgp_api.get_folders_tree()
                write_f.write(tree + '\0')
            
            elif com[0] == "get_folder_content":
                content = dgp_api.get_folder_content(com[1])
                write_f.write(content + '\0')
            
            elif com[0] == "get_file":
                if dgp_api.get_file(com[1], com[2]) == "err":
                    write_f.write('err' + '\0')
                else:
                    write_f.write('OK' + '\0')
            
            elif com[0] == "create_folder":
                folder_id = dgp_api.create_folder(com[1], com[2])
                write_f.write(folder_id + '\0')
            
            elif com[0] == "rename_object":
                is_file = com[1] == '1'
                if dgp_api.rename_object(com[2], com[3], is_file) == "err":
                    write_f.write('err' + '\0')
                else:
                    write_f.write('OK' + '\0')
            
            elif com[0] == "delete_object":
                is_file = com[1] == '1'
                if dgp_api.delete_object(com[2], is_file) == "err":
                    write_f.write('err' + '\0')
                else:
                    write_f.write('OK' + '\0')
            
            elif com[0] == "move_object":
                is_file = com[1] == '1'
                if com[3] == '':
                    dest_folder_id = None
                else:
                    dest_folder_id = com[3]
                
                if dgp_api.move_object(com[2], dest_folder_id, is_file) == "err":
                    write_f.write('err' + '\0')
                else:
                    write_f.write('OK' + '\0')
            
            elif com[0] == "upload_file":
                file_id = dgp_api.move_object(com[1], com[2], com[3], com[4])
                write_f.write(file_id + '\0')
            
            else:
                print("Unknown command", com[0], "with parameters", buffer[:-1].split(b'\0')[1:])
                write_f.write('err' + '\0')
            
            buffer = read_f.readline()
        
        print("Pipe closed by client. Exiting...")
        dgp_api.disconnect()
        read_f.close()
        write_f.close()
        
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
